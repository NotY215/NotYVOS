#include <kernel/font/font.hpp>
#include <kernel/libk/string.hpp>
#include <kernel/log.hpp>

namespace notyvos::font
{

void self_test() noexcept
{
    Face* f = default_face();
    if (!f)
    {
        log::write(log::Level::Warn, "font", "no default face loaded; skipping TTF self-test");
        return;
    }

    const Metrics m = metrics(f, 16);
    log::write(log::Level::Info, "font",
               "metrics@16: ascent=%lld descent=%lld line_gap=%lld line_height=%lld",
               static_cast<long long>(m.ascent), static_cast<long long>(m.descent),
               static_cast<long long>(m.line_gap), static_cast<long long>(m.line_height));

    const char* s = "Hello, NOTYVOS!";
    const i32 w = text_width(f, s, 16);
    log::write(log::Level::Info, "font", "text_width(\"%s\", 16) = %lld px", s,
               static_cast<long long>(w));

    const Glyph* gA = glyph(f, 'A', 16);
    const bool a_ok = gA && gA->coverage && gA->width > 0 && gA->height > 0;

    const i32 space_adv = advance(f, ' ', 16);
    const Glyph* gsp = glyph(f, ' ', 16);
    const bool space_ok = (space_adv > 0) && (!gsp || !gsp->coverage);

    const u64 cached = glyphs_cached(f);
    const bool cache_ok = cached >= 2;

    const bool ok = a_ok && space_ok && cache_ok && (w > 0);
    log::write(ok ? log::Level::Info : log::Level::Warn, "font",
               "TTF self-test: %s (A=%s space=%s cache=%llu width=%lld)", ok ? "PASS" : "FAIL",
               a_ok ? "ok" : "fail", space_ok ? "ok" : "fail",
               static_cast<unsigned long long>(cached), static_cast<long long>(w));
}

} // namespace notyvos::font
