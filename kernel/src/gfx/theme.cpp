#include <kernel/gfx/theme.hpp>

namespace notyvos::gfx::theme
{

namespace
{

constexpr Palette kPalettes[] = {
    // Dark
    {
        "NotYVOS Dark", 0x00101018, 0x000A0A10, 0x001C1C22, 0x004C4C56, 0x00202028, 0x001A1A20,
        0x00F2F2F4,     0x00808088, 0x005A5A64, 0x00303038, 0x00181820, 0x00E4E4E8, 0x00909098,
        0x0060A0E8,     0x00242430, 0x00404050, 0x00E8E8EC, 0x00383840, 0x00C42B1C, 0x00404048,
        0x00203050,     0x00406080, 0x006090C0, 0x00FFFFFF, 0x00000000,
    },
    // Light
    {
        "NotYVOS Light", 0x00D8E4F0, 0x00B8C8DC, 0x00E8ECF0, 0x00D0D8E0, 0x00F4F6F8, 0x00E4E8EC,
        0x00202028,      0x00686878, 0x00A0A8B0, 0x00C0C4C8, 0x00FFFFFF, 0x00202028, 0x00606070,
        0x003070C0,      0x00F8F8FC, 0x00E0E4E8, 0x00202028, 0x00D0D4D8, 0x00C42B1C, 0x00D8DCE0,
        0x00C0D8F0,      0x00A0C0E8, 0x0070A8E0, 0x00202028, 0x00FFFFFF,
    },
    // macOS-inspired dark
    {
        "macOS Dark", 0x00202028, 0x00181820, 0x00282832, 0x003C3C48, 0x00303038, 0x00282830,
        0x00F0F0F4,   0x0090909C, 0x00484854, 0x00303038, 0x00242430, 0x00ECECF0, 0x00A0A0AC,
        0x000A84FF,   0x00282832, 0x00384050, 0x00F0F0F4, 0x003A3A44, 0x00FF453A, 0x00383848,
        0x00202838,   0x00384058, 0x000A84FF, 0x00FFFFFF, 0x00101018,
    },
};

Id g_current = Id::Dark;

} // namespace

const Palette& current() noexcept
{
    return kPalettes[static_cast<u32>(g_current)];
}
Id current_id() noexcept
{
    return g_current;
}
void set(Id id) noexcept
{
    g_current = id;
}
const Palette& get(Id id) noexcept
{
    return kPalettes[static_cast<u32>(id)];
}
u32 count() noexcept
{
    return sizeof(kPalettes) / sizeof(kPalettes[0]);
}

} // namespace notyvos::gfx::theme
