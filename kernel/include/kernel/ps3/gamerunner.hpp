#pragma once
#include <kernel/types.hpp>

namespace notyvos::ps3::gamerunner
{

enum class Format : u8
{
    Unknown = 0,
    ElfPS3,
    ElfNative,
    RawBinary,
};

struct LaunchResult
{
    Format format;
    uptr entry;
    uptr stack_top;
    uptr cr3;
    uptr user_lo;
    uptr user_hi;
    bool loaded;
    bool warning_shown;
    // 7B: PS3 ELF path runs in the PPU interpreter.
    u64 ppu_steps;
    bool ppu_ran;
};

Format detect(const void* data, usize size) noexcept;
Format detect_bin(const void* data, usize size) noexcept;

LaunchResult launch_from_memory(const void* data, usize size, const char* name) noexcept;
LaunchResult launch_from_path(const char* vfs_path) noexcept;

} // namespace notyvos::ps3::gamerunner
