#pragma once
#include <kernel/types.hpp>

namespace notyvos::gfx
{

// Register the VBE (direct-framebuffer) backend. Returns true if the
// framebuffer was successfully registered.
bool vbe_backend_init() noexcept;

// Direct VRAM blit. Copies a source rectangle into the hardware
// framebuffer. `dst_phys` must point at the top-left of the framebuffer.
void vbe_blit(const u32* src, u32 src_w, u32 src_h, u32 src_pitch, u32 dst_x, u32 dst_y, u32 copy_w,
              u32 copy_h) noexcept;

// Query the number of PCI display controllers found.
u32 vbe_gpu_count() noexcept;

} // namespace notyvos::gfx
