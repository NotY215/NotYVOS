#include <kernel/fs/vfs.hpp>
#include <kernel/libk/mem.hpp>
#include <kernel/libk/string.hpp>
#include <kernel/log.hpp>
#include <kernel/ps3/abi/syscalls.hpp>
#include <kernel/sched/scheduler.hpp>

namespace notyvos::ps3::abi
{

namespace
{
u64 g_handled = 0;
u64 g_unknown = 0;
u64 g_exits = 0;

constexpr u32 kSysnoReg = 3;
constexpr u32 kArg0Reg = 4;
constexpr u32 kArg1Reg = 5;
constexpr u32 kArg2Reg = 6;
constexpr u32 kRetReg = 3;

constexpr u32 kMaxPs3Fds = 32;

enum class FdKind : u8
{
    None = 0,
    File = 1,
    Dir = 2,
};

struct Ps3Fd
{
    bool used;
    FdKind kind;
    fs::VNode* vnode;
    usize off;
};

Ps3Fd g_fds[kMaxPs3Fds] = {};

i32 alloc_fd(fs::VNode* vn, FdKind kind) noexcept
{
    for (u32 i = 0; i < kMaxPs3Fds; ++i)
        if (!g_fds[i].used)
        {
            g_fds[i] = {true, kind, vn, 0};
            return static_cast<i32>(i);
        }
    return -1;
}

void free_fd(i32 fd) noexcept
{
    if (fd < 0 || fd >= static_cast<i32>(kMaxPs3Fds))
        return;
    g_fds[fd] = {false, FdKind::None, nullptr, 0};
}

bool read_guest_str(ppu::Context* ctx, u64 addr, char* out, usize cap) noexcept
{
    if (!ctx->read8)
        return false;
    for (usize i = 0; i < cap - 1; ++i)
    {
        u8 c = 0;
        if (!ctx->read8(ctx->user, addr + i, &c))
            return false;
        out[i] = static_cast<char>(c);
        if (c == 0)
            return true;
    }
    out[cap - 1] = 0;
    return true;
}

bool write_guest_bytes(ppu::Context* ctx, u64 addr, const void* src, usize n) noexcept
{
    if (!ctx->write8)
        return false;
    const u8* p = static_cast<const u8*>(src);
    for (usize i = 0; i < n; ++i)
        if (!ctx->write8(ctx->user, addr + i, p[i]))
            return false;
    return true;
}

// Map a VFS path. Handles the fact that guest paths are rooted at "/"
// but the host VFS uses "/" for initramfs and "/disk" for NYFS.
// Guest paths starting with "/app_home" map to "/disk".
fs::VNode* map_path(const char* guest_path, const char* cwd) noexcept
{
    if (!guest_path || !guest_path[0])
        return nullptr;

    // Direct mapping first.
    fs::VNode* vn = fs::vfs_lookup(guest_path, cwd ? cwd : "/");
    if (vn)
        return vn;

    // "/app_home/..." -> "/disk/..."
    const char* prefix = "/app_home";
    u32 i = 0;
    while (prefix[i] && guest_path[i] == prefix[i])
        ++i;
    if (prefix[i] == 0)
    {
        const char* rest = guest_path + i;
        if (*rest == '/')
            ++rest;
        char mapped[256];
        u32 m = 0;
        const char* root = "/disk/";
        while (root[m])
        {
            mapped[m] = root[m];
            ++m;
        }
        while (*rest && m < 255)
            mapped[m++] = *rest++;
        mapped[m] = 0;
        vn = fs::vfs_lookup(mapped, "/");
        if (vn)
            return vn;
    }

    // "/dev_hdd0/..." -> "/disk/..."
    const char* prefix2 = "/dev_hdd0";
    i = 0;
    while (prefix2[i] && guest_path[i] == prefix2[i])
        ++i;
    if (prefix2[i] == 0)
    {
        const char* rest = guest_path + i;
        if (*rest == '/')
            ++rest;
        char mapped[256];
        u32 m = 0;
        const char* root = "/disk/";
        while (root[m])
        {
            mapped[m] = root[m];
            ++m;
        }
        while (*rest && m < 255)
            mapped[m++] = *rest++;
        mapped[m] = 0;
        vn = fs::vfs_lookup(mapped, "/");
        if (vn)
            return vn;
    }

    return nullptr;
}

i64 fd_read(ppu::Context* ctx, i32 fd, u64 buf, u32 n) noexcept
{
    if (fd < 0 || fd >= static_cast<i32>(kMaxPs3Fds) || !g_fds[fd].used)
        return -1;
    if (g_fds[fd].kind != FdKind::File)
        return -1;
    auto* vn = g_fds[fd].vnode;
    if (!vn || !vn->ops || !vn->ops->read)
        return -1;

    isize total = 0;
    u8 tmp[256];
    while (total < static_cast<isize>(n))
    {
        const usize chunk = (n - static_cast<u32>(total)) < sizeof(tmp)
                                ? (n - static_cast<u32>(total))
                                : sizeof(tmp);
        const isize r = vn->ops->read(vn, tmp, g_fds[fd].off, chunk);
        if (r <= 0)
            break;
        if (!write_guest_bytes(ctx, buf + static_cast<u64>(total), tmp, static_cast<usize>(r)))
            break;
        g_fds[fd].off += static_cast<usize>(r);
        total += r;
    }
    return total;
}

i64 fd_write(ppu::Context* ctx, i32 fd, u64 buf, u32 n) noexcept
{
    if (fd < 0 || fd >= static_cast<i32>(kMaxPs3Fds) || !g_fds[fd].used)
        return -1;
    if (g_fds[fd].kind != FdKind::File)
        return -1;
    auto* vn = g_fds[fd].vnode;
    if (!vn || !vn->ops || !vn->ops->write)
        return -1;

    isize total = 0;
    u8 tmp[256];
    while (total < static_cast<isize>(n))
    {
        const usize chunk = (n - static_cast<u32>(total)) < sizeof(tmp)
                                ? (n - static_cast<u32>(total))
                                : sizeof(tmp);
        if (!ctx->read8)
            return -1;
        for (usize i = 0; i < chunk; ++i)
        {
            u8 c = 0;
            if (!ctx->read8(ctx->user, buf + static_cast<u64>(total) + i, &c))
                return -1;
            tmp[i] = c;
        }
        const isize w = vn->ops->write(vn, tmp, g_fds[fd].off, chunk);
        if (w <= 0)
            break;
        g_fds[fd].off += static_cast<usize>(w);
        total += w;
    }
    return total;
}

} // namespace

void install(ppu::Context* ctx) noexcept
{
    if (!ctx)
        return;
    ctx->syscall = &dispatch;
    ctx->user = ctx;
}

bool dispatch(void* user, ppu::Context* ctx) noexcept
{
    (void)user;
    const u64 nrc = ctx->gpr[kSysnoReg];
    const u64 a0 = ctx->gpr[kArg0Reg];
    const u64 a1 = ctx->gpr[kArg1Reg];
    const u64 a2 = ctx->gpr[kArg2Reg];

    switch (nrc)
    {
    case nr::kProcessExit:
        ++g_exits;
        log::write(log::Level::Info, "ps3-abi", "sys_process_exit(%llu)",
                   static_cast<unsigned long long>(a0));
        ctx->gpr[kRetReg] = 0;
        return false;

    case nr::kProcessFork:
        ctx->gpr[kRetReg] = 1;
        ++g_handled;
        return true;

    case nr::kProcessGetPid:
        ctx->gpr[kRetReg] = 1;
        ++g_handled;
        return true;

    case nr::kRead:
    case nr::kCellFsRead:
    {
        const i32 fd = static_cast<i32>(a0);
        const i64 r = fd_read(ctx, fd, a1, static_cast<u32>(a2));
        ctx->gpr[kRetReg] = static_cast<u64>(r);
        ++g_handled;
        return true;
    }

    case nr::kWrite:
    case nr::kCellFsWrite:
    {
        const i32 fd = static_cast<i32>(a0);
        const i64 r = fd_write(ctx, fd, a1, static_cast<u32>(a2));
        ctx->gpr[kRetReg] = static_cast<u64>(r);
        ++g_handled;
        return true;
    }

    case nr::kOpen:
    case nr::kCellFsOpen:
    {
        char path[256];
        if (!read_guest_str(ctx, a0, path, sizeof(path)))
        {
            ctx->gpr[kRetReg] = static_cast<u64>(-1);
            ++g_handled;
            return true;
        }
        auto* vn = map_path(path, "/");
        if (!vn)
        {
            ctx->gpr[kRetReg] = static_cast<u64>(-1);
            ++g_handled;
            return true;
        }
        const i32 fd = alloc_fd(vn, FdKind::File);
        ctx->gpr[kRetReg] = static_cast<u64>(static_cast<i64>(fd));
        ++g_handled;
        return true;
    }

    case nr::kClose:
    case nr::kCellFsClose:
    case nr::kCellFsClosedir:
    {
        free_fd(static_cast<i32>(a0));
        ctx->gpr[kRetReg] = 0;
        ++g_handled;
        return true;
    }

    case nr::kLseek:
    case nr::kCellFsLseek:
    {
        const i32 fd = static_cast<i32>(a0);
        if (fd < 0 || fd >= static_cast<i32>(kMaxPs3Fds) || !g_fds[fd].used)
        {
            ctx->gpr[kRetReg] = static_cast<u64>(-1);
            ++g_handled;
            return true;
        }
        const i64 off = static_cast<i64>(a1);
        const u32 whence = static_cast<u32>(a2);
        isize end = 0;
        if (g_fds[fd].vnode && g_fds[fd].vnode->ops && g_fds[fd].vnode->ops->size)
            end = g_fds[fd].vnode->ops->size(g_fds[fd].vnode);
        switch (whence)
        {
        case 0:
            g_fds[fd].off = static_cast<usize>(off < 0 ? 0 : off);
            break;
        case 1:
            g_fds[fd].off = static_cast<usize>(static_cast<i64>(g_fds[fd].off) + off < 0
                                                   ? 0
                                                   : static_cast<i64>(g_fds[fd].off) + off);
            break;
        case 2:
            g_fds[fd].off = static_cast<usize>(static_cast<i64>(end) + off);
            break;
        default:
            ctx->gpr[kRetReg] = static_cast<u64>(-1);
            ++g_handled;
            return true;
        }
        ctx->gpr[kRetReg] = static_cast<u64>(static_cast<i64>(g_fds[fd].off));
        ++g_handled;
        return true;
    }

    case nr::kStat:
    case nr::kFstat:
    case nr::kCellFsStat:
    {
        fs::VNode* vn = nullptr;
        if (nrc == nr::kFstat)
        {
            const i32 fd = static_cast<i32>(a0);
            if (fd >= 0 && fd < static_cast<i32>(kMaxPs3Fds) && g_fds[fd].used)
                vn = g_fds[fd].vnode;
        }
        else
        {
            char path[256];
            if (read_guest_str(ctx, a0, path, sizeof(path)))
                vn = map_path(path, "/");
        }
        if (!vn)
        {
            ctx->gpr[kRetReg] = static_cast<u64>(-1);
            ++g_handled;
            return true;
        }
        GuestStat gs{};
        gs.mode = (vn->type == fs::VType::Dir) ? 0x41EDu : 0x81A4u;
        gs.uid = 0;
        gs.gid = 0;
        if (vn->ops && vn->ops->size)
            gs.size = static_cast<u32>(vn->ops->size(vn) & 0xFFFFFFFFu);
        const u64 target = (nrc == nr::kFstat) ? a1 : a1;
        (void)write_guest_bytes(ctx, target, &gs, sizeof(gs));
        ctx->gpr[kRetReg] = 0;
        ++g_handled;
        return true;
    }

    case nr::kCellFsOpendir:
    {
        char path[256];
        if (!read_guest_str(ctx, a0, path, sizeof(path)))
        {
            ctx->gpr[kRetReg] = static_cast<u64>(-1);
            ++g_handled;
            return true;
        }
        auto* vn = map_path(path, "/");
        if (!vn || vn->type != fs::VType::Dir)
        {
            ctx->gpr[kRetReg] = static_cast<u64>(-1);
            ++g_handled;
            return true;
        }
        const i32 fd = alloc_fd(vn, FdKind::Dir);
        ctx->gpr[kRetReg] = static_cast<u64>(static_cast<i64>(fd));
        ++g_handled;
        return true;
    }

    case nr::kCellFsReaddir:
    {
        const i32 fd = static_cast<i32>(a0);
        const u32 idx = static_cast<u32>(a1);
        const u64 out_addr = a2;
        if (fd < 0 || fd >= static_cast<i32>(kMaxPs3Fds) || !g_fds[fd].used ||
            g_fds[fd].kind != FdKind::Dir)
        {
            ctx->gpr[kRetReg] = static_cast<u64>(-1);
            ++g_handled;
            return true;
        }
        auto* vn = g_fds[fd].vnode;
        if (!vn || !vn->ops || !vn->ops->readdir)
        {
            ctx->gpr[kRetReg] = static_cast<u64>(-1);
            ++g_handled;
            return true;
        }
        fs::DirEntry e{};
        const isize r = vn->ops->readdir(vn, idx, &e);
        if (r <= 0)
        {
            ctx->gpr[kRetReg] = 0;
            ++g_handled;
            return true;
        }
        GuestDirent gd{};
        u32 n = 0;
        while (e.name[n] && n < 255)
        {
            gd.name[n] = e.name[n];
            ++n;
        }
        gd.name[n] = 0;
        gd.type = (e.type == fs::VType::Dir) ? 1u : 0u;
        gd.size = e.size;
        (void)write_guest_bytes(ctx, out_addr, &gd, sizeof(gd));
        ctx->gpr[kRetReg] = 1;
        ++g_handled;
        return true;
    }

    case nr::kCellFsUnlink:
    {
        char path[256];
        if (!read_guest_str(ctx, a0, path, sizeof(path)))
        {
            ctx->gpr[kRetReg] = static_cast<u64>(-1);
            ++g_handled;
            return true;
        }
        auto* vn = map_path(path, "/");
        if (!vn)
        {
            ctx->gpr[kRetReg] = static_cast<u64>(-1);
            ++g_handled;
            return true;
        }
        const int r = fs::vfs_unlink(vn);
        ctx->gpr[kRetReg] = static_cast<u64>(static_cast<i64>(r));
        ++g_handled;
        return true;
    }

    case nr::kCellFsMkdir:
    {
        char path[256];
        if (!read_guest_str(ctx, a0, path, sizeof(path)))
        {
            ctx->gpr[kRetReg] = static_cast<u64>(-1);
            ++g_handled;
            return true;
        }
        // Simple: create a file entry with the same name. NYFS is flat.
        auto* parent = map_path("/disk", "/");
        if (!parent)
        {
            ctx->gpr[kRetReg] = static_cast<u64>(-1);
            ++g_handled;
            return true;
        }
        const int r = fs::vfs_create(parent, path);
        ctx->gpr[kRetReg] = static_cast<u64>(static_cast<i64>(r));
        ++g_handled;
        return true;
    }

    case nr::kCellFsRename:
    {
        // Not implemented: NYFS rename needs a handle-based API. Return
        // failure so guest code can fall back.
        ctx->gpr[kRetReg] = static_cast<u64>(-1);
        ++g_handled;
        return true;
    }

    case nr::kFirmwareGet:
    {
        usize sz = 0;
        const void* data = fs::vfs_firmware(&sz);
        if (!data)
        {
            ctx->gpr[kRetReg] = static_cast<u64>(-1);
            ++g_handled;
            return true;
        }
        // a0 = pointer to a 16-byte guest slot. Write address then size.
        u64 addr = reinterpret_cast<u64>(data);
        u64 size = static_cast<u64>(sz);
        (void)write_guest_bytes(ctx, a0, &addr, sizeof(addr));
        (void)write_guest_bytes(ctx, a0 + 8, &size, sizeof(size));
        ctx->gpr[kRetReg] = 0;
        ++g_handled;
        return true;
    }

    default:
        ++g_unknown;
        log::write(log::Level::Warn, "ps3-abi", "unhandled syscall %llu (a0=0x%llx)",
                   static_cast<unsigned long long>(nrc), static_cast<unsigned long long>(a0));
        ctx->gpr[kRetReg] = static_cast<u64>(-1);
        ++g_handled;
        return true;
    }
}

u64 calls_handled() noexcept
{
    return g_handled;
}
u64 calls_unknown() noexcept
{
    return g_unknown;
}
u64 process_exits() noexcept
{
    return g_exits;
}

} // namespace notyvos::ps3::abi
