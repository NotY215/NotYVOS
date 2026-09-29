#include <kernel/ps3/powerpc/decode.hpp>

namespace notyvos::ps3::powerpc {

namespace {

inline i32 sign_extend_16(u32 v) noexcept {
    return (v & 0x8000u) ? static_cast<i32>(v | 0xFFFF0000u)
                         : static_cast<i32>(v);
}

inline i32 sign_extend_24(u32 v) noexcept {
    return (v & 0x00800000u) ? static_cast<i32>(v | 0xFF000000u)
                             : static_cast<i32>(v);
}

// Classify a primary-31 instruction by extended opcode.
Form xo_to_form(u32 xo) noexcept {
    switch (xo) {
        // Arithmetic with OE bit (XO form)
        case   8: case  10: case  11: case  40: case 104: case 136:
        case 138: case 200: case 202: case 232: case 234: case 235:
        case 266: case 459: case 488: case 491: case 778: case 826:
            return Form::XO;

        // SPR access (XFX form)
        case 339: case 467:
            return Form::XFX;

        // Shifts (XS form)
        case 24: case 536: case 792:
            return Form::XS;

        // Everything else in the primary 31 group uses X form
        default:
            return Form::X;
    }
}

const char* name_x(u32 xo) noexcept {
    switch (xo) {
        case  28: return "and";
        case  60: return "andc";
        case 124: return "nor";
        case 284: return "eqv";
        case 316: return "xor";
        case 412: return "orc";
        case 444: return "or";
        case 476: return "nand";
        case 792: return "sraw";
        case 824: return "srawi";
        case 954: return "extsb";
        case 986: return "extsh";
        default:  return nullptr;
    }
}

const char* name_xo(u32 xo) noexcept {
    switch (xo) {
        case   8: return "subfc";
        case  10: return "addc";
        case  11: return "mulhwu";
        case  40: return "subf";
        case 104: return "neg";
        case 136: return "subfe";
        case 138: return "adde";
        case 200: return "subfze";
        case 202: return "addze";
        case 232: return "subfme";
        case 234: return "addme";
        case 235: return "mullw";
        case 266: return "add";
        case 459: return "divwu";
        case 488: return "divw";
        case 491: return "divw";
        case 778: return "divwu";
        case 826: return "divw";
        default:  return nullptr;
    }
}

const char* name_d(u8 op) noexcept {
    switch (op) {
        case 14: return "addi";
        case 15: return "addis";
        case 32: return "lwz";
        case 33: return "lwzu";
        case 34: return "lbz";
        case 35: return "lbzu";
        case 36: return "stw";
        case 37: return "stwu";
        case 38: return "stb";
        case 39: return "stbu";
        case 40: return "lhz";
        case 41: return "lhzu";
        case 42: return "lha";
        case 43: return "lhau";
        case 44: return "sth";
        case 45: return "sthu";
        case 46: return "lmw";
        case 47: return "stmw";
        case 48: return "lfs";
        case 50: return "lfd";
        case 52: return "stfs";
        case 54: return "stfd";
        case 58: return "ld";
        case 62: return "std";
        default: return nullptr;
    }
}

} // namespace

Instruction decode(u32 word) noexcept {
    Instruction ins{};
    ins.word = word;

    ins.opcode = static_cast<u8>((word >> 26) & 0x3Fu);
    ins.rt     = (word >> 21) & 0x1Fu;
    ins.ra     = (word >> 16) & 0x1Fu;
    ins.rb     = (word >> 11) & 0x1Fu;
    ins.rc     = (word & 1u) != 0;

    const u32 imm16 = word & 0xFFFFu;
    ins.uimm = imm16;
    ins.simm = sign_extend_16(imm16);

    switch (ins.opcode) {
        case 18: {
            ins.form = Form::I;
            ins.lk   = (word & 1u) != 0;
            ins.bdisp = sign_extend_24(word & 0x03FFFFFCu);
            ins.mnemonic = ins.lk ? "bl" : "b";
            return ins;
        }
        case 16: {
            ins.form = Form::B;
            ins.xo   = (word >> 21) & 0x1Fu;
            ins.lk   = (word & 1u) != 0;
            ins.bdisp = sign_extend_16(word & 0xFFFCu);
            ins.mnemonic = "bc";
            return ins;
        }
        case 19: {
            ins.form = Form::XL;
            ins.xo   = (word >> 1) & 0x3FFu;
            ins.lk   = (word & 1u) != 0;
            if (ins.xo == 16)       ins.mnemonic = ins.lk ? "bclrl" : "bclr";
            else if (ins.xo == 528) ins.mnemonic = ins.lk ? "bcctrl" : "bcctr";
            return ins;
        }
        case 31: {
            ins.xo = (word >> 1) & 0x3FFu;
            ins.oe = ((word >> 10) & 1u) != 0;
            ins.form = xo_to_form(ins.xo);

            if (ins.form == Form::XFX) {
                const u32 spr_lo = (word >> 16) & 0x1Fu;
                const u32 spr_hi = (word >> 6)  & 0x1Fu;
                ins.spr = (spr_lo << 5) | spr_hi;
                if (ins.xo == 339)      ins.mnemonic = "mfspr";
                else if (ins.xo == 467) ins.mnemonic = "mtspr";
                return ins;
            }

            ins.mnemonic = (ins.form == Form::XO)
                         ? name_xo(ins.xo)
                         : name_x(ins.xo);
            return ins;
        }
        case 30: {
            ins.form = Form::MD;
            ins.xo   = (word >> 1) & 0x3FFu;
            ins.mnemonic = nullptr;
            return ins;
        }
        case 63: {
            ins.form = Form::XO2;
            ins.xo   = (word >> 1) & 0x3FFu;
            ins.oe   = ((word >> 10) & 1u) != 0;
            ins.mnemonic = nullptr;
            return ins;
        }
        default: {
            ins.form = Form::D;
            ins.mnemonic = name_d(ins.opcode);
            return ins;
        }
    }
}

} // namespace notyvos::ps3::powerpc