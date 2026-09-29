#include <kernel/ps3/loader.hpp>
#include <kernel/libk/mem.hpp>
#include <kernel/log.hpp>

namespace notyvos::ps3 {

namespace {

constexpr u8 kElfMagic[4] = { 0x7F, 'E', 'L', 'F' };
constexpr u8 kElfClass64  = 2;
constexpr u8 kElfDataBE   = 2;
constexpr u16 kEmPPC64    = 0x15;
constexpr u16 kEmPPC      = 0x14;

struct Elf64_Ehdr {
    u8  e_ident[16];
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

struct Elf64_Phdr {
    u32 p_type;
    u32 p_flags;
    u64 p_offset;
    u64 p_vaddr;
    u64 p_paddr;
    u64 p_filesz;
    u64 p_memsz;
    u64 p_align;
} __attribute__((packed));

} // namespace

bool is_ps3_executable(const void* image, usize size) noexcept {
    if (!image || size < sizeof(Elf64_Ehdr)) return false;
    const auto* eh = static_cast<const Elf64_Ehdr*>(image);
    if (libk::memcmp(eh->e_ident, kElfMagic, 4) != 0) return false;
    if (eh->e_ident[4] != kElfClass64) return false;
    if (eh->e_ident[5] != kElfDataBE) return false;
    if (eh->e_machine != kEmPPC64 && eh->e_machine != kEmPPC) return false;
    return true;
}

bool parse_ps3_executable(const void* image, usize size, Ps3Program* out) noexcept {
    if (!out) return false;
    if (!is_ps3_executable(image, size)) return false;

    const auto* eh = static_cast<const Elf64_Ehdr*>(image);
    if (eh->e_phentsize != sizeof(Elf64_Phdr)) {
        log::write(log::Level::Warn, "ps3",
            "unexpected e_phentsize: %llu",
            static_cast<unsigned long long>(eh->e_phentsize));
        return false;
    }

    out->entry     = eh->e_entry;
    out->phoff     = eh->e_phoff;
    out->phentsize = eh->e_phentsize;
    out->phnum     = eh->e_phnum;
    out->flags     = eh->e_flags;
    out->file_size = size;
    out->raw       = static_cast<const u8*>(image);

    log::write(log::Level::Info, "ps3",
        "PS3 executable: entry=0x%llx, %u phdr, machine=0x%llx",
        static_cast<unsigned long long>(out->entry),
        static_cast<unsigned long long>(out->phnum),
        static_cast<unsigned long long>(eh->e_machine));

    return true;
}

u32 enum_ps3_segments(const Ps3Program& prog, Ps3Segment* out, u32 max_segments) noexcept {
    if (!out || !prog.raw) return 0;
    u32 count = 0;
    for (u32 i = 0; i < prog.phnum && count < max_segments; ++i) {
        const u64 off = prog.phoff + static_cast<u64>(i) * prog.phentsize;
        if (off + sizeof(Elf64_Phdr) > prog.file_size) break;
        const auto* ph = reinterpret_cast<const Elf64_Phdr*>(prog.raw + off);
        out[count].type   = ph->p_type;
        out[count].flags  = ph->p_flags;
        out[count].vaddr  = ph->p_vaddr;
        out[count].filesz = ph->p_filesz;
        out[count].memsz  = ph->p_memsz;
        out[count].offset = ph->p_offset;
        ++count;
    }
    return count;
}

const char* ps3_arch_name(const Ps3Program&) noexcept { return "ppc64"; }

} // namespace notyvos::ps3