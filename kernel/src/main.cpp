// Limine request objects for NOTYVOS.
//
// We use the macros from limine.h rather than hand-copying the protocol
// magic numbers. Hand-copying is how the base revision magic got mistyped
// in the first place (0x...69 instead of 0x...bdc).

#include <limine.h>
#include <stdint.h>

// ---------------------------------------------------------------------------
// Compat shims for Limine macro naming across protocol revisions.
// ---------------------------------------------------------------------------
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

// ---------------------------------------------------------------------------
// Limine requests
// ---------------------------------------------------------------------------
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

    __attribute__((
        used,
        section(".limine_requests_end"))) static volatile uint64_t limine_requests_end_marker[2] =
        LIMINE_REQUESTS_END_MARKER;

} // extern "C"

// ---------------------------------------------------------------------------
// Kernel entry
// ---------------------------------------------------------------------------

#include <kernel/arch/x86_64/serial.hpp>
#include <kernel/boot/limine.hpp>
#include <kernel/fb/framebuffer.hpp>
#include <kernel/log.hpp>
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
    // 1. Serial first, before anything else can fail.
    arch::x86_64::SerialPort::init(arch::x86_64::SerialPort::kCom1);
    arch::x86_64::SerialPort::write("\n");
    arch::x86_64::SerialPort::write("NOTYVOS kernel alive\n");

    // 2. Verify Limine protocol revision.
    if (LIMINE_BASE_REVISION_SUPPORTED(limine_base_revision) == false)
    {
        arch::x86_64::SerialPort::write("FATAL: Limine base revision not supported\n");
        halt_forever();
    }

    // 3. Fetch boot info.
    const auto info = boot::query();

    // 4. Framebuffer console.
    if (info.framebuffer)
    {
        fb::Framebuffer::init(info.framebuffer);
    }

    log::init();

    log::write(log::Level::Info, "boot", "NOTYVOS %s (%s)", NOTYVOS_VERSION, NOTYVOS_GIT_REV);

    if (info.framebuffer)
    {
        log::write(log::Level::Info, "fb", "framebuffer %ux%u pitch=%u bpp=%u",
                   static_cast<unsigned>(info.framebuffer->width),
                   static_cast<unsigned>(info.framebuffer->height),
                   static_cast<unsigned>(info.framebuffer->pitch),
                   static_cast<unsigned>(info.framebuffer->bpp));
    }
    else
    {
        log::write(log::Level::Warn, "fb", "no framebuffer response");
    }

    if (info.memmap)
    {
        log::write(log::Level::Info, "mm", "memory map entries: %u",
                   static_cast<unsigned>(info.memmap->entry_count));
    }
    else
    {
        log::write(log::Level::Warn, "mm", "no memory map response");
    }

    if (info.hhdm)
    {
        log::write(log::Level::Info, "mm", "HHDM offset: 0x%x",
                   static_cast<unsigned long long>(info.hhdm->offset));
    }
    else
    {
        log::write(log::Level::Warn, "mm", "no HHDM response");
    }

    // 5. Banner on framebuffer.
    if (fb::Framebuffer::ready())
    {
        u32 row = (fb::Framebuffer::height() / (8 * 2)) - 4;
        if (row < 2)
            row = 2;
        fb::Framebuffer::draw_text(2, row, "NOTYVOS", 0x0080C0FF, 0x00101018);
        fb::Framebuffer::draw_text(2, row + 1, "kernel alive", 0x00E0E0E0, 0x00101018);
        fb::Framebuffer::draw_text(2, row + 3, "Phase 0 boot OK. Halting.", 0x00A0A0A0, 0x00101018);
    }

    log::write(log::Level::Info, "boot", "Phase 0 boot OK. Halting.");

    halt_forever();
}
