#pragma once
#include <kernel/types.hpp>

namespace notyvos::ps3::ppu {

// ---------------------------------------------------------------------------
// PPU (PowerPC Processing Unit) runtime model
//
// This is the interpreter stage. Phase 5 replaces the interpreter loop with
// a JIT that translates basic blocks into x86-64. The register file, memory
// interface, and syscall ABI stay the same.
//
// All instructions are 32-bit, big-endian.
// All GPRs, LR, CTR, XER, CR are 64-bit.
// ---------------------------------------------------------------------------

constexpr u32 kGprCount = 32;
constexpr u32 kFprCount = 32;
constexpr u32 kCrFields = 8;
constexpr u32 kCrBitsPerField = 4;

struct Context {
    u64 gpr[kGprCount];
    u64 fpr[kFprCount];    // IEEE 754 double bits
    u64 pc;
    u64 lr;
    u64 ctr;
    u64 xer;               // bit 31 = SO, bit 30 = OV, bit 29 = CA
    u32 cr;                // 8 fields of 4 bits; field 0 in bits 28..31

    // Memory interface. Return false on invalid access.
    bool (*read8) (void* user, u64 addr, u8*  out);
    bool (*read16)(void* user, u64 addr, u16* out);
    bool (*read32)(void* user, u64 addr, u32* out);
    bool (*read64)(void* user, u64 addr, u64* out);
    bool (*write8) (void* user, u64 addr, u8  v);
    bool (*write16)(void* user, u64 addr, u16 v);
    bool (*write32)(void* user, u64 addr, u32 v);
    bool (*write64)(void* user, u64 addr, u64 v);

    // System call dispatch. Return true if the syscall was handled.
    // If false, the runtime treats it as a fatal unknown syscall.
    bool (*syscall)(void* user, Context* ctx);

    void* user;
};

// Zero the context and install no memory callbacks.
void init(Context* ctx) noexcept;

// Execute exactly one instruction. Returns true on success.
// On an unknown instruction or a memory fault, writes a diagnostic via
// log and returns false.
bool step(Context* ctx) noexcept;

// Run until the syscall handler returns false or the step limit is hit.
// Returns the number of instructions executed.
u64 run(Context* ctx, u64 max_steps) noexcept;

// Register-file helpers.
inline u32 cr_field(const Context* c, u32 idx) noexcept {
    return (c->cr >> (28u - 4u * idx)) & 0xFu;
}
inline void cr_set_field(Context* c, u32 idx, u32 v) noexcept {
    const u32 shift = 28u - 4u * idx;
    c->cr = (c->cr & ~(0xFu << shift)) | ((v & 0xFu) << shift);
}

} // namespace notyvos::ps3::ppu