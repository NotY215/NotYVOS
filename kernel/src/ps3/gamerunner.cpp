#include <kernel/fs/vfs.hpp>
#include <kernel/libk/mem.hpp>
#include <kernel/libk/string.hpp>
#include <kernel/log.hpp>
#include <kernel/mm/heap.hpp>
#include <kernel/proc/elf.hpp>
#include <kernel/ps3/abi/syscalls.hpp>
#include <kernel/ps3/gamerunner.hpp>
#include <kernel/ps3/loader.hpp>
#include <kernel/ps3/ppu.hpp>

namespace notyvos::ps3::gamerunner
{

namespace
{

constexpr u8 kElfMagic[4] = {0x7F, 'E', 'L', 'F'};

void print_elf_warning(const char* name) noexcept
{
    log::write(log::Level::Warn, "grunner", "=========================");
    log::write(log::Level::Warn, "grunner", "Security Warning");
    log::write(log::Level::Warn, "grunner",
               "This ELF executable (%s) may have been modified or may", name);
    log::write(log::Level::Warn, "grunner", "contain unauthorized or malicious code.");
    log::write(log::Level::Warn, "grunner", "Only run ELF files from trusted/legal sources.");
    log::write(log::Level::Warn, "grunner",
               "[Continuing automatically; GUI modal is a Phase 7C task]");
    log::write(log::Level::Warn, "grunner", "=========================");
}

// --- PPU memory backing for a PS3 ELF -------------------------------------

constexpr u64 kGuestSize = 16ULL * 1024 * 1024;

alignas(64) u8 g_guest[kGuestSize];

bool guest_read8(void*, u64 a, u8* o) noexcept
{
    if (a >= kGuestSize)
        return false;
    *o = g_guest[a];
    return true;
}
bool guest_read16(void*, u64 a, u16* o) noexcept
{
    if (a + 2 > kGuestSize)
        return false;
    *o = static_cast<u16>((static_cast<u16>(g_guest[a]) << 8) | g_guest[a + 1]);
    return true;
}
bool guest_read32(void*, u64 a, u32* o) noexcept
{
    if (a + 4 > kGuestSize)
        return false;
    *o = (static_cast<u32>(g_guest[a]) << 24) | (static_cast<u32>(g_guest[a + 1]) << 16) |
         (static_cast<u32>(g_guest[a + 2]) << 8) | static_cast<u32>(g_guest[a + 3]);
    return true;
}
bool guest_read64(void*, u64 a, u64* o) noexcept
{
    u32 hi = 0, lo = 0;
    if (!guest_read32(nullptr, a, &hi))
        return false;
    if (!guest_read32(nullptr, a + 4, &lo))
        return false;
    *o = (static_cast<u64>(hi) << 32) | lo;
    return true;
}
bool guest_write8(void*, u64 a, u8 v) noexcept
{
    if (a >= kGuestSize)
        return false;
    g_guest[a] = v;
    return true;
}
bool guest_write16(void*, u64 a, u16 v) noexcept
{
    if (a + 2 > kGuestSize)
        return false;
    g_guest[a] = static_cast<u8>((v >> 8) & 0xFF);
    g_guest[a + 1] = static_cast<u8>(v & 0xFF);
    return true;
}
bool guest_write32(void*, u64 a, u32 v) noexcept
{
    if (a + 4 > kGuestSize)
        return false;
    g_guest[a] = static_cast<u8>((v >> 24) & 0xFF);
    g_guest[a + 1] = static_cast<u8>((v >> 16) & 0xFF);
    g_guest[a + 2] = static_cast<u8>((v >> 8) & 0xFF);
    g_guest[a + 3] = static_cast<u8>(v & 0xFF);
    return true;
}
bool guest_write64(void*, u64 a, u64 v) noexcept
{
    if (!guest_write32(nullptr, a, static_cast<u32>(v >> 32)))
        return false;
    if (!guest_write32(nullptr, a + 4, static_cast<u32>(v & 0xFFFFFFFFu)))
        return false;
    return true;
}

// Load PT_LOAD segments into g_guest at their virtual addresses.
// Returns the entry PC, or 0 on failure.
u64 load_segments_into_guest(const Ps3Program& prog) noexcept
{
    Ps3Segment segs[16];
    const u32 n = enum_ps3_segments(prog, segs, 16);
    if (n == 0)
        return 0;

    for (u32 i = 0; i < n; ++i)
    {
        if (segs[i].type != 1u)
            continue; // PT_LOAD
        if (segs[i].filesz == 0)
            continue;
        const u64 vaddr = segs[i].vaddr;
        const u64 fsz = segs[i].filesz;
        const u64 msz = segs[i].memsz;
        const u64 off = segs[i].offset;
        if (vaddr + msz > kGuestSize)
            return 0;
        if (off + fsz > prog.file_size)
            return 0;

        for (u64 b = 0; b < fsz; ++b)
            g_guest[vaddr + b] = prog.raw[off + b];
        for (u64 b = fsz; b < msz; ++b)
            g_guest[vaddr + b] = 0;
    }
    return prog.entry;
}

} // namespace

Format detect_bin(const void* data, usize size) noexcept
{
    if (!data || size < 4)
        return Format::Unknown;
    const auto* b = static_cast<const u8*>(data);
    if (b[0] == 0x53 && b[1] == 0x43 && b[2] == 0x45 && b[3] == 0x00)
        return Format::RawBinary;
    if (size >= 5 && b[0] == 0x00 && b[1] == 0x52 && b[2] == 0x50 && b[3] == 0x4B && b[4] == 0x47)
        return Format::RawBinary;
    return Format::Unknown;
}

Format detect(const void* data, usize size) noexcept
{
    if (!data || size < 4)
        return Format::Unknown;
    const auto* b = static_cast<const u8*>(data);
    if (libk::memcmp(b, kElfMagic, 4) != 0)
        return detect_bin(data, size);
    if (size < 20)
        return Format::Unknown;

    const u8 cls = b[4];
    const u8 end = b[5];
    if (cls != 2)
        return Format::Unknown;

    u16 machine = 0;
    if (end == 1)
        machine = static_cast<u16>(b[18] | (static_cast<u16>(b[19]) << 8));
    else if (end == 2)
        machine = static_cast<u16>((static_cast<u16>(b[18]) << 8) | b[19]);
    else
        return Format::Unknown;

    if (machine == 0x15 || machine == 0x14)
        return Format::ElfPS3;
    if (machine == 0x3E)
        return Format::ElfNative;
    return Format::Unknown;
}

LaunchResult launch_from_memory(const void* data, usize size, const char* name) noexcept
{
    LaunchResult r{};
    r.format = detect(data, size);

    if (r.format == Format::Unknown)
    {
        log::write(log::Level::Warn, "grunner", "unrecognised format for '%s'",
                   name ? name : "(unnamed)");
        return r;
    }

    if (r.format == Format::ElfPS3)
    {
        print_elf_warning(name);
        r.warning_shown = true;

        Ps3Program prog{};
        if (!parse_ps3_executable(data, size, &prog))
        {
            log::write(log::Level::Error, "grunner", "PS3 ELF parse failed");
            return r;
        }

        const u64 entry = load_segments_into_guest(prog);
        if (entry == 0)
        {
            log::write(log::Level::Error, "grunner",
                       "segment load failed (image too large for guest RAM?)");
            return r;
        }

        ppu::Context ctx{};
        ppu::init(&ctx);
        ctx.read8 = guest_read8;
        ctx.read16 = guest_read16;
        ctx.read32 = guest_read32;
        ctx.read64 = guest_read64;
        ctx.write8 = guest_write8;
        ctx.write16 = guest_write16;
        ctx.write32 = guest_write32;
        ctx.write64 = guest_write64;
        ctx.pc = entry;
        abi::install(&ctx);

        // Bound the run: this is a Phase 7B smoke run, not a game session.
        constexpr u64 kMaxSteps = 100000;
        r.ppu_steps = ppu::run(&ctx, kMaxSteps);
        r.ppu_ran = true;
        r.entry = entry;

        log::write(log::Level::Info, "grunner",
                   "PS3 ELF '%s': entry=0x%llx steps=%llu handled=%llu unknown=%llu exits=%llu",
                   name ? name : "(unnamed)", static_cast<unsigned long long>(entry),
                   static_cast<unsigned long long>(r.ppu_steps),
                   static_cast<unsigned long long>(abi::calls_handled()),
                   static_cast<unsigned long long>(abi::calls_unknown()),
                   static_cast<unsigned long long>(abi::process_exits()));
        r.loaded = true;
        return r;
    }

    if (r.format == Format::ElfNative)
    {
        log::write(log::Level::Info, "grunner",
                   "native x86-64 ELF '%s': loading via proc::load_elf", name ? name : "(unnamed)");
    }
    else
    {
        log::write(log::Level::Warn, "grunner",
                   "'%s' is a raw container (SELF/RPKG); PS3 ABI support lands in 7C",
                   name ? name : "(unnamed)");
        return r;
    }

    const auto elf = proc::load_elf(data, size);
    if (!elf.entry)
    {
        log::write(log::Level::Error, "grunner", "ELF load failed");
        return r;
    }

    r.entry = elf.entry;
    r.stack_top = elf.stack_top;
    r.cr3 = elf.cr3;
    r.user_lo = elf.user_lo;
    r.user_hi = elf.user_hi;
    r.loaded = true;

    log::write(log::Level::Info, "grunner",
               "prepared '%s': entry=0x%llx cr3=0x%llx user=[0x%llx,0x%llx)",
               name ? name : "(unnamed)", static_cast<unsigned long long>(r.entry),
               static_cast<unsigned long long>(r.cr3), static_cast<unsigned long long>(r.user_lo),
               static_cast<unsigned long long>(r.user_hi));
    return r;
}

LaunchResult launch_from_path(const char* vfs_path) noexcept
{
    LaunchResult r{};
    if (!vfs_path)
        return r;

    auto* vn = fs::vfs_lookup(vfs_path, "/");
    if (!vn || !vn->ops || !vn->ops->size || !vn->ops->read)
    {
        log::write(log::Level::Warn, "grunner", "not found: %s", vfs_path);
        return r;
    }

    const isize sz = vn->ops->size(vn);
    if (sz <= 0 || sz > 64 * 1024 * 1024)
    {
        log::write(log::Level::Warn, "grunner", "size out of range: %lld",
                   static_cast<long long>(sz));
        return r;
    }

    auto* buf = static_cast<u8*>(mm::Heap::allocate(static_cast<usize>(sz)));
    if (!buf)
        return r;

    isize got = 0;
    while (got < sz)
    {
        const isize n =
            vn->ops->read(vn, buf + got, static_cast<usize>(got), static_cast<usize>(sz - got));
        if (n <= 0)
            break;
        got += n;
    }
    if (got != sz)
    {
        mm::Heap::deallocate(buf);
        return r;
    }

    r = launch_from_memory(buf, static_cast<usize>(sz), vfs_path);
    mm::Heap::deallocate(buf);
    return r;
}

} // namespace notyvos::ps3::gamerunner
