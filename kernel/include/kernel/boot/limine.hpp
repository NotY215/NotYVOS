#pragma once

// This header wraps the Limine protocol so that the rest of the kernel
// does not have to include limine.h directly. It also gives us a single
// place to declare Limine request objects.

#include <kernel/types.hpp>

// Pull in the official Limine header (provided by the build tree).
#include <limine.h>

namespace notyvos::boot {

struct BootInfo {
    limine_framebuffer*         framebuffer;
    limine_memmap_response*     memmap;
    limine_hhdm_response*       hhdm;
};

// Fetches the responses from Limine requests and packages them.
// Returns BootInfo by value; caller should treat missing fields as fatal
// only when the corresponding subsystem is initialized.
BootInfo query() noexcept;

} // namespace notyvos::boot