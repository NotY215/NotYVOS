#pragma once
#include <kernel/types.hpp>

namespace notyvos::ps3::dma {

// ---------------------------------------------------------------------------
// Cell DMA engine
//
// Models the MFC (Memory Flow Controller) transfer queue. Tags identify
// in-flight transfers so callers can wait on completion by tag.
//
// The real Cell has 16 DMA channels per SPE. Phase 4D implements a single
// shared queue with tag-based completion events. Per-channel arbitration
// arrives with the scheduler integration in Phase 5.
// ---------------------------------------------------------------------------

enum class Dir : u8 { MainToLocal, LocalToMain };

constexpr u32 kMaxInFlight = 16;

struct Desc {
    u32  tag;
    Dir  dir;
    u64  main_addr;
    u32  ls_addr;
    u32  size;
    u8*  ls_base;         // the local-store buffer to read/write
    u32  ls_capacity;     // buffer size, checked against ls_addr + size
    bool in_use;
};

struct Engine {
    Desc descs[kMaxInFlight];
    u32  next_tag_bitmap;      // bit i set = tag i has a completed transfer
    u8*  main_memory;          // flat main memory backing for the test/demo
    u64  main_size;
    u64  transferred_bytes;
    u32  completed_count;
    u32  error_count;
};

void init(Engine* eng) noexcept;
void set_backing(Engine* eng, u8* main_memory, u64 size) noexcept;

// Queue one transfer. Returns false if no slot is available or bounds fail.
bool queue(Engine* eng, u32 tag, Dir dir, u64 main_addr, u32 ls_addr,
           u32 size, u8* ls_base, u32 ls_capacity) noexcept;

// Perform up to `max` pending transfers. Returns the number completed.
u32 drain(Engine* eng, u32 max) noexcept;

// True if the given tag has completed at least once since the last clear.
bool tag_completed(const Engine* eng, u32 tag) noexcept;
void clear_tag(Engine* eng, u32 tag) noexcept;

// Wait for a specific tag to complete. Returns false on error or if the
// tag never completes within the step budget.
bool wait_tag(Engine* eng, u32 tag, u32 max_drain_rounds) noexcept;

// Memory ordering helpers. Backed by hardware barriers on x86-64.
void atomic_fence() noexcept;
void sync_barrier() noexcept;

} // namespace notyvos::ps3::dma