#pragma once
#include <kernel/types.hpp>

namespace notyvos::gfx::icons
{

enum class Id : u8
{
    Unknown = 0,
    Explorer,
    Settings,
    Terminal,
    GameLauncher,
    Bin,
    Profile,
    StartButton,
    Loading, // spinner used for launch transitions
    Count
};

// Load all 256x256 ICO icons from the initramfs at /icons/*.ico. Called from
// Compositor::init() after the VFS is mounted.
void init() noexcept;

// Native bitmap for `id`, or nullptr if the icon failed to load.
const u32* bitmap(Id id) noexcept;
u32 bitmap_size(Id id) noexcept;

// Blit the icon at (x, y) with the given target size. Uses nearest-neighbour
// scaling. No-op if the icon is missing.
void draw(Id id, i32 x, i32 y, u32 size) noexcept;

} // namespace notyvos::gfx::icons
