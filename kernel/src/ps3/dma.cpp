#include <kernel/ps3/dma.hpp>
#include <kernel/libk/mem.hpp>
#include <kernel/log.hpp>

namespace notyvos::ps3::dma {

namespace {

Desc* find_free(Engine* eng) noexcept {
    for (u32 i = 0; i < kMaxInFlight; ++i) {
        if (!eng->descs[i].in_use) return &eng->descs[i];
    }
    return nullptr;
}

bool perform(Engine* eng, Desc& d) noexcept {
    if (!eng->main_memory) {
        log::write(log::Level::Warn, "ps3-dma", "no main memory backing");
        return false;
    }

    // Bounds check on the local side.
    if (static_cast<u64>(d.ls_addr) + d.size > d.ls_capacity) {
        log::write(log::Level::Warn, "ps3-dma",
            "local bounds fail: ls_addr=%u size=%u capacity=%u",
            static_cast<unsigned long long>(d.ls_addr),
            static_cast<unsigned long long>(d.size),
            static_cast<unsigned long long>(d.ls_capacity));
        return false;
    }

    // Bounds check on the main side.
    if (d.main_addr + d.size > eng->main_size) {
        log::write(log::Level::Warn, "ps3-dma",
            "main bounds fail: main_addr=0x%llx size=%u main_size=%llu",
            static_cast<unsigned long long>(d.main_addr),
            static_cast<unsigned long long>(d.size),
            static_cast<unsigned long long>(eng->main_size));
        return false;
    }

    u8* main_ptr = eng->main_memory + d.main_addr;
    u8* ls_ptr   = d.ls_base + d.ls_addr;

    switch (d.dir) {
        case Dir::MainToLocal:
            libk::memcpy(ls_ptr, main_ptr, d.size);
            break;
        case Dir::LocalToMain:
            libk::memcpy(main_ptr, ls_ptr, d.size);
            break;
    }

    eng->transferred_bytes += d.size;
    ++eng->completed_count;
    eng->next_tag_bitmap |= (1u << (d.tag & 31u));
    return true;
}

} // namespace

void init(Engine* eng) noexcept {
    for (u32 i = 0; i < kMaxInFlight; ++i) {
        eng->descs[i] = {};
    }
    eng->next_tag_bitmap = 0;
    eng->main_memory = nullptr;
    eng->main_size = 0;
    eng->transferred_bytes = 0;
    eng->completed_count = 0;
    eng->error_count = 0;
}

void set_backing(Engine* eng, u8* main_memory, u64 size) noexcept {
    eng->main_memory = main_memory;
    eng->main_size = size;
}

bool queue(Engine* eng, u32 tag, Dir dir, u64 main_addr, u32 ls_addr,
           u32 size, u8* ls_base, u32 ls_capacity) noexcept {
    if (size == 0) return false;
    if (!ls_base) return false;

    Desc* d = find_free(eng);
    if (!d) {
        log::write(log::Level::Warn, "ps3-dma", "no free slot");
        return false;
    }

    d->tag = tag;
    d->dir = dir;
    d->main_addr = main_addr;
    d->ls_addr = ls_addr;
    d->size = size;
    d->ls_base = ls_base;
    d->ls_capacity = ls_capacity;
    d->in_use = true;
    return true;
}

u32 drain(Engine* eng, u32 max) noexcept {
    u32 done = 0;
    for (u32 i = 0; i < kMaxInFlight && done < max; ++i) {
        Desc& d = eng->descs[i];
        if (!d.in_use) continue;
        if (!perform(eng, d)) {
            ++eng->error_count;
            d.in_use = false;
            continue;
        }
        d.in_use = false;
        ++done;
    }
    return done;
}

bool tag_completed(const Engine* eng, u32 tag) noexcept {
    return (eng->next_tag_bitmap & (1u << (tag & 31u))) != 0;
}

void clear_tag(Engine* eng, u32 tag) noexcept {
    eng->next_tag_bitmap &= ~(1u << (tag & 31u));
}

bool wait_tag(Engine* eng, u32 tag, u32 max_drain_rounds) noexcept {
    for (u32 i = 0; i < max_drain_rounds; ++i) {
        if (tag_completed(eng, tag)) {
            clear_tag(eng, tag);
            return true;
        }
        const u32 done = drain(eng, 1);
        if (done == 0) {
            // Nothing left in the queue and tag is not yet set -- give up.
            return tag_completed(eng, tag);
        }
    }
    return tag_completed(eng, tag);
}

void atomic_fence() noexcept {
    asm volatile("mfence" ::: "memory");
}

void sync_barrier() noexcept {
    asm volatile("mfence\nlfence\nsfence" ::: "memory");
}

} // namespace notyvos::ps3::dma