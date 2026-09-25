#include <limine.h>
#include <stdint.h>

#ifndef LIMINE_FRAMEBUFFER_REQUEST_ID
#ifdef LIMINE_FRAMEBUFFER_REQUEST
#define LIMINE_FRAMEBUFFER_REQUEST_ID LIMINE_FRAMEBUFFER_REQUEST
#endif
#endif
#ifndef LIMINE_MEMMAP_REQUEST_ID
#ifdef LIMINE_MEMMAP_REQUEST
#define LIMINE_MEMMAP_REQUEST_ID LIMINE_MEMMAP_REQUEST
#endif
#endif
#ifndef LIMINE_HHDM_REQUEST_ID
#ifdef LIMINE_HHDM_REQUEST
#define LIMINE_HHDM_REQUEST_ID LIMINE_HHDM_REQUEST
#endif
#endif
#ifndef LIMINE_MP_REQUEST_ID
#ifdef LIMINE_SMP_REQUEST_ID
#define LIMINE_MP_REQUEST_ID LIMINE_SMP_REQUEST_ID
#endif
#endif

extern "C"
{

    __attribute__((used, section(".limine_requests_start"))) static volatile uint64_t
        limine_requests_start_marker[4] = LIMINE_REQUESTS_START_MARKER;

    __attribute__((used,
                   section(".limine_requests"))) static volatile uint64_t limine_base_revision[3] =
        LIMINE_BASE_REVISION(3);

    __attribute__((used, section(".limine_requests"))) static volatile limine_framebuffer_request
        framebuffer_request = {
            .id = LIMINE_FRAMEBUFFER_REQUEST_ID,
            .revision = 0,
            .response = nullptr,
    };

    __attribute__((
        used, section(".limine_requests"))) static volatile limine_memmap_request memmap_request = {
        .id = LIMINE_MEMMAP_REQUEST_ID,
        .revision = 0,
        .response = nullptr,
    };

    __attribute__((
        used, section(".limine_requests"))) static volatile limine_hhdm_request hhdm_request = {
        .id = LIMINE_HHDM_REQUEST_ID,
        .revision = 0,
        .response = nullptr,
    };

    // Limine v12 names this protocol "mp" (multiprocessor).
    __attribute__((used,
                   section(".limine_requests"))) static volatile limine_mp_request mp_request = {
        .id = LIMINE_MP_REQUEST_ID,
        .revision = 0,
        .response = nullptr,
        .flags = 0,
    };

    __attribute__((
        used,
        section(".limine_requests_end"))) static volatile uint64_t limine_requests_end_marker[2] =
        LIMINE_REQUESTS_END_MARKER;

} // extern "C"

#include <kernel/arch/x86_64/cpu.hpp>
#include <kernel/arch/x86_64/isr.hpp>
#include <kernel/arch/x86_64/lapic.hpp>
#include <kernel/arch/x86_64/percpu.hpp>
#include <kernel/arch/x86_64/serial.hpp>
#include <kernel/arch/x86_64/smp.hpp>
#include <kernel/boot/limine.hpp>
#include <kernel/fb/console.hpp>
#include <kernel/fb/framebuffer.hpp>
#include <kernel/log.hpp>
#include <kernel/mm/heap.hpp>
#include <kernel/mm/paging.hpp>
#include <kernel/mm/pmm.hpp>
#include <kernel/mm/vmm.hpp>
#include <kernel/panic.hpp>
#include <kernel/types.hpp>

using namespace notyvos;

namespace notyvos::boot
{

BootInfo query() noexcept
{
    BootInfo info{};
    if (framebuffer_request.response && framebuffer_request.response->framebuffer_count > 0)
    {
        info.framebuffer = framebuffer_request.response->framebuffers[0];
    }
    if (memmap_request.response)
        info.memmap = memmap_request.response;
    if (hhdm_request.response)
        info.hhdm = hhdm_request.response;
    return info;
}

} // namespace notyvos::boot

extern "C" [[noreturn]] void kernel_main()
{
    arch::x86_64::SerialPort::init(arch::x86_64::SerialPort::kCom1);
    arch::x86_64::SerialPort::write("\nNOTYVOS kernel alive\n");

    if (LIMINE_BASE_REVISION_SUPPORTED(limine_base_revision) == false)
    {
        arch::x86_64::SerialPort::write("FATAL: Limine base revision not supported\n");
        halt_forever();
    }

    const auto info = boot::query();
    if (info.framebuffer)
    {
        fb::Framebuffer::init(info.framebuffer);
        fb::Console::init();
    }
    log::init();

    log::write(log::Level::Info, "boot", "NOTYVOS %s (%s)", NOTYVOS_VERSION, NOTYVOS_GIT_REV);

    if (!info.memmap || !info.hhdm)
        panic("Limine memmap/HHDM missing");
    log::write(log::Level::Info, "mm", "memory map entries: %u", info.memmap->entry_count);
    log::write(log::Level::Info, "mm", "HHDM offset: 0x%llx", info.hhdm->offset);

    arch::x86_64::cpu_init();

    mm::PhysicalMemory::init(info.memmap, info.hhdm->offset);
    mm::VirtualMemory::init(info.hhdm->offset);

    mm::Heap::init();

    {
        auto* p = static_cast<u8*>(mm::Heap::allocate(100));
        if (p)
        {
            for (u32 i = 0; i < 100; ++i)
                p[i] = static_cast<u8>(i);
            u32 sum = 0;
            for (u32 i = 0; i < 100; ++i)
                sum += p[i];
            log::write(log::Level::Info, "heap-test", "alloc 100 bytes, checksum=%llu",
                       static_cast<unsigned long long>(sum));
            mm::Heap::deallocate(p);
        }
        else
        {
            log::write(log::Level::Warn, "heap-test", "allocate failed");
        }
    }

    arch::x86_64::percpu_init_bsp();
    arch::x86_64::Lapic::init_bsp();

    arch::x86_64::percpu_register(0, arch::x86_64::Lapic::id(), 0, 0);

    arch::x86_64::smp_init(mp_request.response);

    {
        const u64 k = 0xDEADBEEFCAFEBABEULL;
        log::write(log::Level::Info, "fmt-test",
                   "ptr=%p hex=%llx dec=%llu pad=%032llx str=%s ch=%c", reinterpret_cast<void*>(k),
                   k, k, k, "hello", '!');
    }

    log::write(log::Level::Info, "boot",
               "Phase 1E/1F/1G OK. Enabling interrupts, entering idle loop.");
    arch::x86_64::interrupts_enable();
    for (;;)
        asm volatile("hlt");
}
