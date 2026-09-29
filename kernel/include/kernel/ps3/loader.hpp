#pragma once
#include <kernel/types.hpp>

namespace notyvos::ps3
{

// A PS3 executable is an ELF64 with EM_PPC64 (0x15) as e_machine.
// The loader validates the header and extracts the segment layout without
// mapping anything into memory yet. Phase 4B will map and start executing.
struct Ps3Program
{
    u64 entry;     // e_entry — virtual address of the entry point
    u64 phoff;     // offset of the program header table
    u16 phentsize; // size of one program header entry
    u16 phnum;     // number of program header entries
    u32 flags;     // e_flags (ABI version, etc.)
    usize file_size;
    const u8* raw; // pointer to the raw image bytes
};

struct Ps3Segment
{
    u32 type;  // PT_*
    u32 flags; // PF_*
    u64 vaddr;
    u64 filesz;
    u64 memsz;
    u64 offset;
};

// Recognise a PS3 ELF image. Returns true if it is a valid PowerPC ELF.
bool is_ps3_executable(const void* image, usize size) noexcept;

// Parse the ELF header. On success fills out and returns true.
bool parse_ps3_executable(const void* image, usize size, Ps3Program* out) noexcept;

// Return the number of PT_LOAD segments. `out` is an array of at least
// `max_segments` elements. Returns the number actually filled.
u32 enum_ps3_segments(const Ps3Program& prog, Ps3Segment* out, u32 max_segments) noexcept;

// Human-readable arch name for logging. "ppc64" or "unknown".
const char* ps3_arch_name(const Ps3Program& prog) noexcept;

} // namespace notyvos::ps3
