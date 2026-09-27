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
#ifndef LIMINE_MODULE_REQUEST_ID
#ifdef LIMINE_MODULE_REQUEST
#define LIMINE_MODULE_REQUEST_ID LIMINE_MODULE_REQUEST
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
            .id = LIMINE_FRAMEBUFFER_REQUEST_ID, .revision = 0, .response = nullptr};
    __attribute__((
        used, section(".limine_requests"))) static volatile limine_memmap_request memmap_request = {
        .id = LIMINE_MEMMAP_REQUEST_ID, .revision = 0, .response = nullptr};
    __attribute__((
        used, section(".limine_requests"))) static volatile limine_hhdm_request hhdm_request = {
        .id = LIMINE_HHDM_REQUEST_ID, .revision = 0, .response = nullptr};
    __attribute__((used,
                   section(".limine_requests"))) static volatile limine_mp_request mp_request = {
        .id = LIMINE_MP_REQUEST_ID, .revision = 0, .response = nullptr, .flags = 0};
    __attribute__((
        used, section(".limine_requests"))) static volatile limine_module_request module_request = {
        .id = LIMINE_MODULE_REQUEST_ID,
        .revision = 0,
        .response = nullptr,
        .internal_module_count = 0,
        .internal_modules = nullptr};
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
#include <kernel/fs/initramfs.hpp>
#include <kernel/fs/vfs.hpp>
#include <kernel/log.hpp>
#include <kernel/mm/heap.hpp>
#include <kernel/mm/pmm.hpp>
#include <kernel/mm/vmm.hpp>
#include <kernel/panic.hpp>
#include <kernel/proc/elf.hpp>
#include <kernel/sched/scheduler.hpp>
#include <kernel/types.hpp>

using namespace notyvos;

namespace notyvos::boot
{
BootInfo query() noexcept
{
    BootInfo info{};
    if (framebuffer_request.response && framebuffer_request.response->framebuffer_count > 0)
        info.framebuffer = framebuffer_request.response->framebuffers[0];
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

    if (!LIMINE_BASE_REVISION_SUPPORTED(limine_base_revision))
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
    log::write(log::Level::Info, "mm", "memory map entries: %llu",
               static_cast<unsigned long long>(info.memmap->entry_count));
    log::write(log::Level::Info, "mm", "HHDM offset: 0x%llx",
               static_cast<unsigned long long>(info.hhdm->offset));

    arch::x86_64::cpu_init();
    mm::PhysicalMemory::init(info.memmap, info.hhdm->offset);
    mm::VirtualMemory::init(info.hhdm->offset);
    mm::Heap::init();

    arch::x86_64::percpu_init_bsp();
    arch::x86_64::Lapic::init_bsp(info.hhdm->offset);
    arch::x86_64::percpu_register(0, arch::x86_64::Lapic::id(), 0, 0);
    arch::x86_64::smp_init(mp_request.response);

    sched::scheduler_init();

    // Initramfs = module 0, init.elf = module 1.
    if (!module_request.response || module_request.response->module_count < 2)
    {
        log::write(log::Level::Error, "init",
                   "need 2 Limine modules (initramfs + init.elf); got %llu",
                   module_request.response
                       ? static_cast<unsigned long long>(module_request.response->module_count)
                       : 0ULL);
        for (;;)
            asm volatile("hlt");
    }
    auto* initrd = module_request.response->modules[0];
    auto* elfmod = module_request.response->modules[1];

    fs::vfs_init();
    fs::VNode* root = fs::initramfs_mount(initrd->address, initrd->size);
    if (!root)
        panic("initramfs mount failed");
    fs::vfs_mount_root(root);

    const auto elf = proc::load_elf(elfmod->address, elfmod->size);
    if (!elf.entry)
        panic("ELF load failed");

    auto* init_task = sched::task_create_user("init", elf.entry, elf.stack_top, elf.cr3,
                                              elf.user_lo, elf.user_hi);
    if (!init_task)
        panic("failed to create init task");
    sched::scheduler_add(init_task);

    log::write(log::Level::Info, "boot", "starting scheduler, %llu task(s)",
               static_cast<unsigned long long>(sched::scheduler_task_count()));

    arch::x86_64::interrupts_enable();
    sched::scheduler_start();
    for (;;)
        asm volatile("hlt");
}
