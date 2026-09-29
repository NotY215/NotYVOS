#pragma once
#include <kernel/types.hpp>

namespace notyvos::ps3::spu {

// ---------------------------------------------------------------------------
// SPU (Synergistic Processing Unit) runtime model
//
// Each SPU has 256 KiB of private local store. It cannot access main memory
// directly; all transfers go through DMA. Communication with the PPU uses
// mailboxes and signals.
//
// The interpreter in Phase 4C executes SPU instructions from local store.
// Phase 5 adds SPU-to-x86-64 JIT.
// ---------------------------------------------------------------------------

constexpr u32 kLocalStoreSize = 256 * 1024;   // 256 KiB
constexpr u32 kRegisterCount  = 128;          // 128 128-bit registers
constexpr u32 kMailboxDepth   = 4;

struct Mailbox {
    u32 items[kMailboxDepth];
    u32 head;
    u32 tail;
    u32 count;
};

struct Context {
    // 128-bit registers stored as 4 x u32 lanes (big-endian lane order)
    u32 gpr[kRegisterCount][4];
    u32 pc;                     // byte address inside local store
    u8  local_store[kLocalStoreSize];

    // State
    u32 spu_status;             // SPU_RunCntl / SPU_Status merged
    u32 spu_cfg;
    u32 lslr;                   // local-store limit register
    u32 lsr;                    // local-store address register

    // Mailboxes
    Mailbox inbound;            // PPU -> SPU
    Mailbox outbound;           // SPU -> PPU

    // Event queue
    u32  event_mask;
    bool stopped;
    bool halted;

    // DMA engine interface. Return false if the transfer fails.
    bool (*dma_read) (void* user, u64 main_addr, u32 ls_addr, u32 size, u32 tag);
    bool (*dma_write)(void* user, u64 main_addr, u32 ls_addr, u32 size, u32 tag);
    void* user;
};

// Zero the context.
void init(Context* ctx) noexcept;

// Push a word into the inbound mailbox. Returns false if full.
bool mailbox_push_inbound(Context* ctx, u32 value) noexcept;

// Pop a word from the outbound mailbox. Returns false if empty.
bool mailbox_pop_outbound(Context* ctx, u32* out) noexcept;

// Execute exactly one SPU instruction from local store.
bool step(Context* ctx) noexcept;

// Run up to max_steps instructions, stopping early on halt / stop / error.
u64 run(Context* ctx, u64 max_steps) noexcept;

// Direct local-store helpers. Bounds-checked.
bool ls_read32 (const Context* ctx, u32 addr, u32* out) noexcept;
bool ls_write32(Context* ctx, u32 addr, u32 value) noexcept;

} // namespace notyvos::ps3::spu