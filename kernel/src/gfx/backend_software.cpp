#include <kernel/gfx/hal.hpp>

namespace notyvos::gfx
{

namespace
{

bool sw_init() noexcept
{
    return true;
}
void sw_shutdown() noexcept {}

void sw_begin_frame(HalSurface*) noexcept {}
void sw_end_frame() noexcept {}

const HalBackend g_software_backend = {
    "software", sw_init,       sw_shutdown, sw_begin_frame, sw_end_frame, sw::clear,
    sw::pixel,  sw::fill_rect, sw::hline,   sw::vline,      sw::circle,
};

bool g_registered = false;

} // namespace

void register_software_backend() noexcept
{
    if (g_registered)
        return;
    g_registered = true;
    hal_register_backend(&g_software_backend);
}

} // namespace notyvos::gfx
