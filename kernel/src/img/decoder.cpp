#include <kernel/img/decoder.hpp>
#include <kernel/libk/mem.hpp>
#include <kernel/libk/string.hpp>
#include <kernel/log.hpp>
#include <kernel/mm/heap.hpp>

namespace notyvos::img
{

// Implementations live in bmp.cpp / png.cpp / gif.cpp / ico.cpp. All
// have external linkage so the compiler sees them from here.
bool decode_bmp_impl(const void* data, usize size, Image& out) noexcept;
bool decode_png_impl(const void* data, usize size, Image& out) noexcept;
bool decode_gif_impl(const void* data, usize size, Image& out) noexcept;
bool decode_ico_impl(const void* data, usize size, Image& out) noexcept;
bool decode_jpeg_impl(const void* data, usize size, Image& out) noexcept;

Format detect(const void* data, usize size) noexcept
{
    if (!data || size < 8)
        return Format::Unknown;
    const auto* b = static_cast<const u8*>(data);

    if (b[0] == 'B' && b[1] == 'M')
        return Format::Bmp;

    if (b[0] == 0x89 && b[1] == 'P' && b[2] == 'N' && b[3] == 'G' && b[4] == 0x0D && b[5] == 0x0A &&
        b[6] == 0x1A && b[7] == 0x0A)
        return Format::Png;

    if (b[0] == 0xFF && b[1] == 0xD8 && b[2] == 0xFF)
        return Format::Jpeg;

    if (b[0] == 'G' && b[1] == 'I' && b[2] == 'F' && b[3] == '8')
        return Format::Gif;

    if (b[0] == 0 && b[1] == 0 && b[2] == 1 && b[3] == 0)
        return Format::Ico;

    return Format::Unknown;
}

bool decode(const void* data, usize size, Image& out) noexcept
{
    out = {0, 0, nullptr, false};
    switch (detect(data, size))
    {
    case Format::Bmp:
        return decode_bmp_impl(data, size, out);
    case Format::Png:
        return decode_png_impl(data, size, out);
    case Format::Gif:
        return decode_gif_impl(data, size, out);
    case Format::Ico:
        return decode_ico_impl(data, size, out);

    case Format::Jpeg:
        return decode_jpeg_impl(data, size, out);

    default:
        return false;
    }
}

void free(Image& img) noexcept
{
    if (img.pixels && img.owned)
        mm::Heap::deallocate(img.pixels);
    img = {0, 0, nullptr, false};
}

} // namespace notyvos::img
