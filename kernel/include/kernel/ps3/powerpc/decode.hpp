#pragma once
#include <kernel/types.hpp>

namespace notyvos::ps3::powerpc {

// ---------------------------------------------------------------------------
// PowerPC instruction decoder
//
// Only the identifier and top-level fields are extracted here. Full
// semantics (register operands, immediate fields) are computed by the
// interpreter in Phase 4B.
//
// All PowerPC instructions are 32-bit, big-endian.
// ---------------------------------------------------------------------------

enum class Form : u8 {
    Unknown,
    D,   // load/store/arith with 16-bit immediate (primary 14,15,32..)
    I,   // unconditional branch (primary 18)
    B,   // conditional branch (primary 16)
    XL,  // branch to LR/CTR (primary 19)
    X,   // compare/logical/rotate (primary 31, no OE)
    XO,  // arithmetic with OE bit (primary 31, Rc field)
    XFX, // mtspr/mfspr (primary 31, SPR fields)
    XS,  // shift (primary 31)
    XFL, // floating compare (primary 63)
    XO2, // PPC64 extended (primary 63)
    MD,  // 64-bit rotate/mask (primary 30)
    MDS, // 64-bit shift (primary 30)
};

struct Instruction {
    u32  word;         // raw big-endian 32-bit value
    u8   opcode;       // primary opcode (bits 0..5)
    Form form;
    // Common extracted fields. Meaning depends on form.
    u32  rt;           // bits 6..10  -- target GPR
    u32  ra;           // bits 11..15 -- source GPR / base
    u32  rb;           // bits 16..20 -- second source GPR
    i32  simm;         // sign-extended 16-bit immediate (D-form)
    u32  uimm;         // zero-extended 16-bit immediate (D-form)
    u32  xo;           // extended opcode (bits 21..30 for X/XO/XL)
    u32  spr;          // SPR field for XFX-form
    bool rc;           // record flag (bit 31)
    bool oe;           // overflow-enable (bit 21, XO forms)
    bool lk;           // link (bit 31 for I/B/XL forms)
    i32  bdisp;        // sign-extended branch displacement (I-form LI field)

    // Recognised mnemonic, or nullptr if not identified.
    const char* mnemonic;
};

// Decode one instruction word. `word` is the 32-bit value as it appears in
// memory (big-endian on disk; caller must load it as a u32 with the byte
// order already swapped, or pass the raw host-order value and let the
// decoder know -- see below).
Instruction decode(u32 word_be_host) noexcept;

// Convenience: assemble a 32-bit word from four big-endian bytes.
inline u32 load_be32(const u8* p) noexcept {
    return (static_cast<u32>(p[0]) << 24)
         | (static_cast<u32>(p[1]) << 16)
         | (static_cast<u32>(p[2]) <<  8)
         |  static_cast<u32>(p[3]);
}

} // namespace notyvos::ps3::powerpc