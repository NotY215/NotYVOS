#pragma once
#include <kernel/types.hpp>

namespace notyvos::ps3::jit
{

enum class BlockEnd : u8
{
    FallThrough, // ran out of instruction budget (safety stop)
    Branch,      // last insn was b / bl / bclr / bcctr
    Syscall,     // last insn was `sc`
    Unsupported, // hit an opcode the translator cannot emit
};

constexpr u32 kMaxInsnsPerBlock = 64;

struct Block
{
    u64 ppc_start; // PPC PC of the first translated instruction
    u64 ppc_end;   // PPC PC after the last translated instruction
    u8* x86_code;  // Entry point (ExecArena VA)
    u32 x86_size;  // Bytes
    u32 insns;     // Number of translated PPC instructions
    BlockEnd end;
    Block* next; // Hash chain
};

} // namespace notyvos::ps3::jit
