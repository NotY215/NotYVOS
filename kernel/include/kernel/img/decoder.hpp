#pragma once
#include <kernel/types.hpp>

namespace notyvos::img
{

enum class Format : u8
{
    Unknown = 0,
    Bmp,
    Png,
    Jpeg,
    Gif,
    Ico,
    Raw,
};

struct Image
{
    u32 width;
    u32 height;
    u32* pixels; // 0x00RRGGBB
    bool owned;  // if true, decoder allocated pixels via mm::Heap
};

Format detect(const void* data, usize size) noexcept;

// Returns true on success. On failure, `out.pixels == nullptr`.
bool decode(const void* data, usize size, Image& out) noexcept;

void free(Image& img) noexcept;

} // namespace notyvos::img
