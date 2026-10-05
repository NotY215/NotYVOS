#include <kernel/img/decoder.hpp>
#include <kernel/img/self_test.hpp>
#include <kernel/libk/mem.hpp>
#include <kernel/libk/string.hpp>
#include <kernel/log.hpp>

namespace notyvos::img
{

namespace
{

// ---------------------------------------------------------------------------
// 2x2 24-bit BMP. Header + DIB + two rows of 2 pixels (BGR), padded to 4.
// BMP rows are stored bottom-up. After decoding, row 0 is the *top* row.
// ---------------------------------------------------------------------------
alignas(64) const u8 kBmp2x2[] = {
    'B',
    'M',
    54 + 16,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    54,
    0,
    0,
    0,
    40,
    0,
    0,
    0,
    2,
    0,
    0,
    0,
    2,
    0,
    0,
    0,
    1,
    0,
    24,
    0,
    0,
    0,
    0,
    0,
    16,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    // File row 0 (bottom in BMP): blue, white  -> BGR: 255,0,0 / 255,255,255
    255,
    0,
    0,
    255,
    255,
    255,
    0,
    0,
    // File row 1 (top in BMP):    red,  green  -> BGR: 0,0,255 / 0,255,0
    0,
    0,
    255,
    0,
    255,
    0,
    0,
    0,
};

// ---------------------------------------------------------------------------
// 2x2 true-colour PNG. The IDAT payload is a stored (uncompressed) DEFLATE
// block. Raw scanlines: filter 0 + 6 bytes RGB per pixel, two rows.
// ---------------------------------------------------------------------------
alignas(64) u8 g_png[512];

u32 build_png_2x2() noexcept
{
    auto wr_be = [](u8* p, u32 v)
    {
        p[0] = static_cast<u8>((v >> 24) & 0xFFu);
        p[1] = static_cast<u8>((v >> 16) & 0xFFu);
        p[2] = static_cast<u8>((v >> 8) & 0xFFu);
        p[3] = static_cast<u8>(v & 0xFFu);
    };
    u32 c = 0;

    static const u8 sig[8] = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};
    libk::memcpy(g_png, sig, 8);
    c = 8;

    auto wr_chunk = [&](const char* type, const u8* body, u32 len)
    {
        wr_be(g_png + c, len);
        c += 4;
        libk::memcpy(g_png + c, type, 4);
        c += 4;
        if (body && len)
        {
            libk::memcpy(g_png + c, body, len);
            c += len;
        }
        wr_be(g_png + c, 0);
        c += 4; // CRC (ignored by our decoder)
    };

    u8 ihdr[13];
    wr_be(ihdr + 0, 2); // width
    wr_be(ihdr + 4, 2); // height
    ihdr[8] = 8;        // bit depth
    ihdr[9] = 2;        // colour type RGB
    ihdr[10] = 0;
    ihdr[11] = 0;
    ihdr[12] = 0;
    wr_chunk("IHDR", ihdr, 13);

    // Two raw scanlines: filter 0 + 2*3 bytes of RGB.
    u8 raw[2 * (1 + 6)] = {
        0, 0xFF, 0, 0,    0,    0xFF, 0,   // row 0: red, green
        0, 0,    0, 0xFF, 0xFF, 0xFF, 0xFF // row 1: blue, white
    };
    const u32 raw_len = sizeof(raw);

    // zlib stream around a stored DEFLATE block.
    u8 z[2 + 5 + raw_len + 4];
    u32 zp = 0;
    z[zp++] = 0x78;
    z[zp++] = 0x01;
    z[zp++] = 0x01; // BFINAL=1, BTYPE=00
    z[zp++] = static_cast<u8>(raw_len & 0xFFu);
    z[zp++] = static_cast<u8>((raw_len >> 8) & 0xFFu);
    z[zp++] = static_cast<u8>(~raw_len & 0xFFu);
    z[zp++] = static_cast<u8>((~raw_len >> 8) & 0xFFu);
    for (u32 i = 0; i < raw_len; ++i)
        z[zp++] = raw[i];
    z[zp++] = 0;
    z[zp++] = 0;
    z[zp++] = 0;
    z[zp++] = 0; // Adler32 placeholder

    wr_chunk("IDAT", z, zp);
    wr_chunk("IEND", nullptr, 0);
    return c;
}

// ---------------------------------------------------------------------------
// Minimal 1x1 JPEG for format detection only. The entropy stream is not
// valid; we only verify that the format detector recognises the header.
// ---------------------------------------------------------------------------
alignas(64) const u8 kJpeg1x1[] = {
    0xFF, 0xD8,                   // SOI
    0xFF, 0xE0, 0x00, 0x10,       // APP0, length 16
    0x4A, 0x46, 0x49, 0x46, 0x00, // "JFIF\0"
    0x01, 0x01,                   // version 1.1
    0x00,                         // density units
    0x00, 0x01, 0x00, 0x01,       // X/Y density
    0x00, 0x00,                   // thumbnail
};

} // namespace

void self_test() noexcept
{
    // ---- BMP ----
    {
        Image img{};
        const bool ok = decode(kBmp2x2, sizeof(kBmp2x2), img) && img.width == 2 &&
                        img.height == 2 && img.pixels[0] == 0x000000FFu // top-left = blue
                        && img.pixels[1] == 0x00FFFFFFu                 // top-right = white
                        && img.pixels[2] == 0x00FF0000u                 // bottom-left = red
                        && img.pixels[3] == 0x0000FF00u;                // bottom-right = green
        if (img.pixels)
            free(img);
        log::write(ok ? log::Level::Info : log::Level::Warn, "img", "BMP decoder self-test: %s",
                   ok ? "PASS" : "FAIL");
    }

    // ---- PNG ----
    {
        const u32 png_len = build_png_2x2();
        Image png{};
        const bool ok = decode(g_png, png_len, png) && png.width == 2 && png.height == 2 &&
                        png.pixels[0] == 0x00FF0000u && png.pixels[1] == 0x0000FF00u &&
                        png.pixels[2] == 0x000000FFu && png.pixels[3] == 0x00FFFFFFu;
        if (png.pixels)
            free(png);
        log::write(ok ? log::Level::Info : log::Level::Warn, "img", "PNG decoder self-test: %s",
                   ok ? "PASS" : "FAIL");
    }

    // ---- JPEG (detector only) ----
    {
        const bool jpeg_detected = (detect(kJpeg1x1, sizeof(kJpeg1x1)) == Format::Jpeg);
        log::write(jpeg_detected ? log::Level::Info : log::Level::Warn, "img",
                   "JPEG detector test: %s", jpeg_detected ? "PASS" : "FAIL");
    }
}

} // namespace notyvos::img
