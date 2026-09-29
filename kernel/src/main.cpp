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
#include <kernel/block/ahci.hpp>
#include <kernel/block/block.hpp>
#include <kernel/boot/limine.hpp>
#include <kernel/fb/console.hpp>
#include <kernel/fb/framebuffer.hpp>
#include <kernel/fs/initramfs.hpp>
#include <kernel/fs/nyfs.hpp>
#include <kernel/fs/vfs.hpp>
#include <kernel/log.hpp>
#include <kernel/libk/string.hpp>
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

    block::block_init();
    block::ahci_init();

    // Block self-test: write a pattern to the last sector, read it back.
    if (block::block_count() > 0)
    {
        auto* dev = block::block_get(0);
        if (dev && !dev->read_only && dev->sector_count > 8)
        {
            static u8 pattern[512];
            static u8 readback[512];
            for (u32 i = 0; i < 512; ++i)
                pattern[i] = static_cast<u8>(i ^ 0xA5);

            const u64 test_lba = dev->sector_count - 1;
            const isize wr = block::block_write(dev, test_lba, 1, pattern);
            const isize rd = block::block_read(dev, test_lba, 1, readback);

            bool match = (wr == 512 && rd == 512);
            if (match)
            {
                for (u32 i = 0; i < 512; ++i)
                {
                    if (pattern[i] != readback[i])
                    {
                        match = false;
                        break;
                    }
                }
            }
            log::write(log::Level::Info, "blk-test", "dev=%s lba=%llu wr=%lld rd=%lld match=%s",
                       dev->name, static_cast<unsigned long long>(test_lba),
                       static_cast<long long>(wr), static_cast<long long>(rd),
                       match ? "yes" : "no");
        }
    }

    // Mount NYFS on the first block device if there is one.
    fs::VNode* nyfs_root = nullptr;
    if (block::block_count() > 0)
    {
        nyfs_root = fs::nyfs_mount(block::block_get(0));
    }

    arch::x86_64::percpu_init_bsp();
    arch::x86_64::Lapic::init_bsp(info.hhdm->offset);
    arch::x86_64::percpu_register(0, arch::x86_64::Lapic::id(), 0, 0);
    arch::x86_64::smp_init(mp_request.response);

    sched::scheduler_init();

    if (!module_request.response || module_request.response->module_count < 2)
    {
        log::write(log::Level::Error, "init", "need 2 Limine modules");
        for (;;)
            asm volatile("hlt");
    }
    auto* initrd = module_request.response->modules[0];
    auto* elfmod = module_request.response->modules[1];

    fs::vfs_init();
    fs::VNode* root = fs::initramfs_mount(initrd->address, initrd->size);
    if (!root)
        panic("initramfs mount failed");

    // Attach the NYFS root directly as /disk. Do not move its children into
    // a separate wrapper VNode: nyfs_rescan() rebuilds g_root->children, and
    // if that node is not in the VFS tree, new files never become visible.
    if (nyfs_root)
    {
        // Rename nyfs_root to "disk" and attach to VFS root.
        const char* newname = "disk";
        u32 i = 0;
        while (newname[i] && i < sizeof(nyfs_root->name) - 1)
        {
            nyfs_root->name[i] = newname[i];
            ++i;
        }
        nyfs_root->name[i] = 0;
        fs::vnode_attach(root, nyfs_root);
    }

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
