#include <kernel/libk/mem.hpp>
#include <kernel/log.hpp>
#include <kernel/ps3/loader.hpp>

namespace notyvos::ps3
{

namespace
{

// PS3 ELF files are big-endian. The host is little-endian, so every
// multi-byte field must be byte-swapped explicitly. The `Elf64_Ehdr`
// and `Elf64_Phdr` structs are kept for size reference only; fields
// are read through the helpers below.

constexpr u8 kElfMagic[4] = {0x7F, 'E', 'L', 'F'};
constexpr u8 kElfClass64 = 2;
constexpr u8 kElfDataBE = 2;
constexpr u16 kEmPPC64 = 0x15;
constexpr u16 kEmPPC = 0x14;

struct Elf64_Ehdr
{
    u8 e_ident[16];
    u16 e_type;
    u16 e_machine;
    u32 e_version;
    u64 e_entry;
    u64 e_phoff;
    u64 e_shoff;
    u32 e_flags;
    u16 e_ehsize;
    u16 e_phentsize;
    u16 e_phnum;
    u16 e_shentsize;
    u16 e_shnum;
    u16 e_shstrndx;
} __attribute__((packed));

struct Elf64_Phdr
{
    u32 p_type;
    u32 p_flags;
    u64 p_offset;
    u64 p_vaddr;
    u64 p_paddr;
    u64 p_filesz;
    u64 p_memsz;
    u64 p_align;
} __attribute__((packed));

static_assert(sizeof(Elf64_Ehdr) == 64, "Elf64_Ehdr must be 64 bytes");
static_assert(sizeof(Elf64_Phdr) == 56, "Elf64_Phdr must be 56 bytes");

// Offsets within Elf64_Ehdr, as byte offsets into the raw image.
constexpr usize kOffEMachine = 18;
constexpr usize kOffEEntry = 24;
constexpr usize kOffEPhOff = 32;
constexpr usize kOffEFlags = 48;
constexpr usize kOffEPhentsize = 54;
constexpr usize kOffEPhnum = 56;

// Offsets within Elf64_Phdr.
constexpr usize kOffPType = 0;
constexpr usize kOffPFlags = 4;
constexpr usize kOffPOffset = 8;
constexpr usize kOffPVaddr = 16;
constexpr usize kOffPFilesz = 32;
constexpr usize kOffPMemsz = 40;

inline u16 rd_be16(const u8* p) noexcept
{
    return static_cast<u16>((static_cast<u32>(p[0]) << 8) | static_cast<u32>(p[1]));
}

inline u32 rd_be32(const u8* p) noexcept
{
    return (static_cast<u32>(p[0]) << 24) | (static_cast<u32>(p[1]) << 16) |
           (static_cast<u32>(p[2]) << 8) | static_cast<u32>(p[3]);
}

inline u64 rd_be64(const u8* p) noexcept
{
    return (static_cast<u64>(rd_be32(p)) << 32) | static_cast<u64>(rd_be32(p + 4));
}

} // namespace

bool is_ps3_executable(const void* image, usize size) noexcept
{
    if (!image || size < sizeof(Elf64_Ehdr))
        return false;
    const auto* b = static_cast<const u8*>(image);
    if (libk::memcmp(b, kElfMagic, 4) != 0)
        return false;
    if (b[4] != kElfClass64)
        return false;
    if (b[5] != kElfDataBE)
        return false;
    const u16 machine = rd_be16(b + kOffEMachine);
    if (machine != kEmPPC64 && machine != kEmPPC)
        return false;
    return true;
}

bool parse_ps3_executable(const void* image, usize size, Ps3Program* out) noexcept
{
    if (!out)
        return false;
    if (!is_ps3_executable(image, size))
        return false;

    const auto* b = static_cast<const u8*>(image);

    const u16 phentsize = rd_be16(b + kOffEPhentsize);
    if (phentsize != sizeof(Elf64_Phdr))
    {
        log::write(log::Level::Warn, "ps3", "unexpected e_phentsize: %llu",
                   static_cast<unsigned long long>(phentsize));
        return false;
    }

    const u16 machine = rd_be16(b + kOffEMachine);

    out->entry = rd_be64(b + kOffEEntry);
    out->phoff = rd_be64(b + kOffEPhOff);
    out->phentsize = phentsize;
    out->phnum = rd_be16(b + kOffEPhnum);
    out->flags = rd_be32(b + kOffEFlags);
    out->file_size = size;
    out->raw = b;

    log::write(log::Level::Info, "ps3", "PS3 executable: entry=0x%llx, %u phdr, machine=0x%llx",
               static_cast<unsigned long long>(out->entry),
               static_cast<unsigned long long>(out->phnum),
               static_cast<unsigned long long>(machine));

    return true;
}

u32 enum_ps3_segments(const Ps3Program& prog, Ps3Segment* out, u32 max_segments) noexcept
{
    if (!out || !prog.raw)
        return 0;
    u32 count = 0;
    for (u32 i = 0; i < prog.phnum && count < max_segments; ++i)
    {
        const u64 off = prog.phoff + static_cast<u64>(i) * static_cast<u64>(prog.phentsize);
        if (off + static_cast<u64>(sizeof(Elf64_Phdr)) > static_cast<u64>(prog.file_size))
            break;
        const u8* ph = prog.raw + off;
        out[count].type = rd_be32(ph + kOffPType);
        out[count].flags = rd_be32(ph + kOffPFlags);
        out[count].offset = rd_be64(ph + kOffPOffset);
        out[count].vaddr = rd_be64(ph + kOffPVaddr);
        out[count].filesz = rd_be64(ph + kOffPFilesz);
        out[count].memsz = rd_be64(ph + kOffPMemsz);
        ++count;
    }
    return count;
}

const char* ps3_arch_name(const Ps3Program&) noexcept
{
    return "ppc64";
}

} // namespace notyvos::ps3
