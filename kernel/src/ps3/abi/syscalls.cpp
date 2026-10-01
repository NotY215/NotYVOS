#include <kernel/fs/vfs.hpp>
#include <kernel/libk/mem.hpp>
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
struct Ps3Fd
{
    bool used;
    fs::VNode* vnode;
    usize off;
};
Ps3Fd g_fds[kMaxPs3Fds] = {};

i32 alloc_fd(fs::VNode* vn) noexcept
{
    for (u32 i = 0; i < kMaxPs3Fds; ++i)
        if (!g_fds[i].used)
        {
            g_fds[i] = {true, vn, 0};
            return static_cast<i32>(i);
        }
    return -1;
}

void free_fd(i32 fd) noexcept
{
    if (fd < 0 || fd >= static_cast<i32>(kMaxPs3Fds))
        return;
    g_fds[fd] = {false, nullptr, 0};
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
    const u64 nr = ctx->gpr[kSysnoReg];
    const u64 a0 = ctx->gpr[kArg0Reg];
    const u64 a1 = ctx->gpr[kArg1Reg];
    const u64 a2 = ctx->gpr[kArg2Reg];

    switch (nr)
    {
    case nr::kProcessExit:
        ++g_exits;
        log::write(log::Level::Info, "ps3-abi", "sys_process_exit(%llu)",
                   static_cast<unsigned long long>(a0));
        ctx->gpr[kRetReg] = 0;
        return false;

    case nr::kProcessFork:
        log::write(log::Level::Info, "ps3-abi", "sys_process_fork");
        ctx->gpr[kRetReg] = 1;
        ++g_handled;
        return true;

    case nr::kRead:
    case nr::kWrite:
        ctx->gpr[kRetReg] = 0;
        ++g_handled;
        return true;

    case nr::kOpen:
    case nr::kClose:
        ctx->gpr[kRetReg] = 0;
        ++g_handled;
        return true;

    case nr::kGetPid:
        ctx->gpr[kRetReg] = 1;
        ++g_handled;
        return true;

    case nr::kCellFsOpen:
    {
        char path[256];
        if (!read_guest_str(ctx, a0, path, sizeof(path)))
        {
            ctx->gpr[kRetReg] = static_cast<u64>(-1);
            return true;
        }
        auto* vn = fs::vfs_lookup(path, "/");
        if (!vn)
        {
            ctx->gpr[kRetReg] = static_cast<u64>(-1);
            ++g_handled;
            return true;
        }
        const i32 fd = alloc_fd(vn);
        ctx->gpr[kRetReg] = static_cast<u64>(static_cast<i64>(fd));
        ++g_handled;
        return true;
    }

    case nr::kCellFsRead:
    {
        const i32 fd = static_cast<i32>(a0);
        const u64 buf = a1;
        const u32 n = static_cast<u32>(a2);
        if (fd < 0 || fd >= static_cast<i32>(kMaxPs3Fds) || !g_fds[fd].used)
        {
            ctx->gpr[kRetReg] = static_cast<u64>(-1);
            return true;
        }
        auto* vn = g_fds[fd].vnode;
        if (!vn || !vn->ops || !vn->ops->read)
        {
            ctx->gpr[kRetReg] = static_cast<u64>(-1);
            return true;
        }
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
            if (!ctx->write8)
                break;
            for (isize i = 0; i < r; ++i)
                (void)ctx->write8(ctx->user, buf + static_cast<u64>(total + i), tmp[i]);
            g_fds[fd].off += static_cast<usize>(r);
            total += r;
        }
        ctx->gpr[kRetReg] = static_cast<u64>(total);
        ++g_handled;
        return true;
    }

    case nr::kCellFsWrite:
    {
        const i32 fd = static_cast<i32>(a0);
        if (fd < 0 || fd >= static_cast<i32>(kMaxPs3Fds) || !g_fds[fd].used)
        {
            ctx->gpr[kRetReg] = static_cast<u64>(-1);
            return true;
        }
        ctx->gpr[kRetReg] = 0;
        ++g_handled;
        return true;
    }

    case nr::kCellFsClose:
    {
        const i32 fd = static_cast<i32>(a0);
        free_fd(fd);
        ctx->gpr[kRetReg] = 0;
        ++g_handled;
        return true;
    }

    default:
        ++g_unknown;
        log::write(log::Level::Warn, "ps3-abi", "unhandled syscall %llu (a0=0x%llx)",
                   static_cast<unsigned long long>(nr), static_cast<unsigned long long>(a0));
        ctx->gpr[kRetReg] = static_cast<u64>(-1);
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
