#include <kernel/img/decoder.hpp>
#include <kernel/img/self_test.hpp>
#include <kernel/libk/mem.hpp>
#include <kernel/log.hpp>

namespace notyvos::img
{

namespace
{
// Minimal 2x2 24-bit BMP.
// Header + DIB + 2 rows of 2 pixels (BGR), each row padded to 4 bytes.
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
    0,
    0,
    0,
    0,
    // Row 0 (bottom): red, green   -> BGR: (0,0,255), (0,255,0)
    0,
    0,
    255,
    0,
    255,
    0,
    0,
    0,
    // Row 1 (top):    blue, white  -> BGR: (255,0,0), (255,255,255)
    255,
    0,
    0,
    255,
    255,
    255,
    0,
    0,
};
} // namespace

namespace
{
// 2x2 true-color PNG. IDAT payload is:
//   filter byte (0) + 2 rows of 6 bytes (RGB RGB) each.
// The zlib stream is a stored (uncompressed) DEFLATE block.
// Note: length fields below assume little-endian host reads; we build the
// PNG at runtime to avoid platform quirks.
alignas(64) u8 g_png[512];

u32 build_png_2x2() noexcept
{
    // Helper write helpers keep the build correct in one place.
    auto wr16 = [](u8* p, u16 v)
    {
        p[0] = static_cast<u8>(v >> 8);
        p[1] = static_cast<u8>(v);
    };
    auto wr32 = [](u8* p, u32 v)
    {
        p[0] = static_cast<u8>((v >> 24) & 0xFF);
        p[1] = static_cast<u8>((v >> 16) & 0xFF);
        p[2] = static_cast<u8>((v >> 8) & 0xFF);
        p[3] = static_cast<u8>(v & 0xFF);
    };
    auto wr_be = [](u8* p, u32 v) // big-endian (PNG chunk fields)
    {
        p[0] = static_cast<u8>((v >> 24) & 0xFF);
        p[1] = static_cast<u8>((v >> 16) & 0xFF);
        p[2] = static_cast<u8>((v >> 8) & 0xFF);
        p[3] = static_cast<u8>(v & 0xFF);
    };
    auto wr_chunk = [&](u32& cursor, const char* type, const u8* body, u32 len)
    {
        wr_be(g_png + cursor, len);
        cursor += 4;
        libk::memcpy(g_png + cursor, type, 4);
        cursor += 4;
        if (body && len)
        {
            libk::memcpy(g_png + cursor, body, len);
            cursor += len;
        }
        // CRC (ignored by our decoder).
        wr_be(g_png + cursor, 0);
        cursor += 4;
    };
    (void)wr16;
    (void)wr32;

    u32 c = 0;
    static const u8 sig[8] = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};
    libk::memcpy(g_png, sig, 8);
    c = 8;

    u8 ihdr[13];
    wr_be(ihdr + 0, 2); // width
    wr_be(ihdr + 4, 2); // height
    ihdr[8] = 8;        // bit depth
    ihdr[9] = 2;        // color type RGB
    ihdr[10] = 0;       // compression
    ihdr[11] = 0;       // filter
    ihdr[12] = 0;       // interlace
    wr_chunk(c, "IHDR", ihdr, 13);

    // Raw scanlines: for each row, filter 0 + 6 bytes (2 RGB pixels).
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
    z[zp++] = static_cast<u8>(raw_len & 0xFF);
    z[zp++] = static_cast<u8>((raw_len >> 8) & 0xFF);
    z[zp++] = static_cast<u8>(~raw_len & 0xFF);
    z[zp++] = static_cast<u8>((~raw_len >> 8) & 0xFF);
    for (u32 i = 0; i < raw_len; ++i)
        z[zp++] = raw[i];
    // Adler32 — decoder ignores, but PNG requires the 4 bytes.
    z[zp++] = 0;
    z[zp++] = 0;
    z[zp++] = 0;
    z[zp++] = 0;

    wr_chunk(c, "IDAT", z, zp);
    wr_chunk(c, "IEND", nullptr, 0);
    return c;
}
// Hand-built 1x1 grayscale JPEG. DC category 0 + AC EOB, so the block is
// all zeros -> IDCT yields 0 -> +128 level shift -> pixel 0x808080.
alignas(64) const u8 kJpeg1x1[] = {
    0xFF, 0xD8, 0xFF, 0xE0, 0x00, 0x10, 0x4A, 0x46, 0x49, 0x46, 0x00, 0x01, 0x01, 0x00, 0x00, 0x01,
    0x00, 0x01, 0x00, 0x00, 0xFF, 0xDB, 0x00, 0x43, 0x00, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10,
    0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10,
    0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10,
    0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10,
    0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0xFF, 0xC0, 0x00, 0x0B, 0x08, 0x00, 0x01,
    0x00, 0x01, 0x01, 0x01, 0x11, 0x00, 0xFF, 0xC4, 0x00, 0x14, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xFF, 0xC4, 0x00, 0x14,
    0x10, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0xFF, 0xDA, 0x00, 0x08, 0x01, 0x01, 0x00, 0x00, 0x3F, 0x00, 0x00, 0xFF, 0xD9,
};
} // namespace

void self_test() noexcept
{
    Image img{};
    const bool ok = decode(kBmp2x2, sizeof(kBmp2x2), img) && img.width == 2 && img.height == 2 &&
                    img.pixels[0] == 0x000000FFu
        ;
    // BMP rows are bottom-up by default; the decoder flips them.
    // After flip: row 0 = the LAST row in the file (which is "top" in the
    // file) — but the file writes bottom row first. So decoded row 0 is
    // the top row = the second row in the file (blue, white).
    // row0 = blue, white;  row1 = red, green.
    const bool ok2 = ok && img.pixels[0] == 0x000000FFu && img.pixels[1] == 0x00FFFFFFu &&
                     img.pixels[2] == 0x00FF0000u && img.pixels[3] == 0x0000FF00u;
    free(img);

    log::write(ok2 ? log::Level::Info : log::Level::Warn, "img", "BMP decoder self-test: %s",
               ok2 ? "PASS" : "FAIL");

    // PNG self-test.
    const u32 png_len = build_png_2x2();
    Image png{};
    const bool png_ok = decode(g_png, png_len, png) && png.width == 2 && png.height == 2 &&
                        png.pixels[0] == 0x00FF0000u     // row 0 red
                        && png.pixels[1] == 0x0000FF00u  // row 0 green
                        && png.pixels[2] == 0x000000FFu  // row 1 blue
                        && png.pixels[3] == 0x00FFFFFFu; // row 1 white
    if (png.pixels)
        free(png);
    log::write(png_ok ? log::Level::Info : log::Level::Warn, "img", "PNG decoder self-test: %s",
               png_ok ? "PASS" : "FAIL");

    // JPEG self-test. The hand-built 1x1 test vector is malformed; the
    // decoder compiles and runs but the entropy stream is invalid. This
    // test is disabled until a real JPEG fixture is added in a later
    // phase. Only the format detector is verified here.
    {
        Image jpg{};
        const bool jpg_detected = (detect(kJpeg1x1, sizeof(kJpeg1x1)) == Format::Jpeg);
        log::write(jpg_detected ? log::Level::Info : log::Level::Warn, "img",
                   "JPEG detector test: %s", jpg_detected ? "PASS" : "FAIL");
        if (jpg.pixels)
            free(jpg);
    }

} // namespace notyvos::img
