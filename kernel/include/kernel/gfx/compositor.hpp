#pragma once
#include <kernel/gfx/theme.hpp>
#include <kernel/types.hpp>

namespace notyvos::gfx
{

constexpr u32 kMaxWindows = 8;
constexpr u32 kWinTitleMax = 24;

enum class WindowKind : u8
{
    Terminal,
    Explorer,
    Settings,
    Bin,
    ImageViewer,
    GameLauncher,
    GameRuntime, // RSX guest surface presented by GameRunner
    Generic
};
enum class AnimState : u8
{
    Settled = 0,
    Opening = 1,
    Closing = 2,
    Minimizing = 3,
    Restoring = 4,
};

struct Window
{
    i32 x;
    i32 y;
    i32 w;
    i32 h;
    bool visible;
    bool focused;
    bool minimized;
    bool maximized;
    i32 saved_x, saved_y, saved_w, saved_h;
    WindowKind kind;
    char title[kWinTitleMax];

    AnimState anim_state;
    u64 anim_start_tick;
};

class Compositor
{
public:
    static void notify(const char* text) noexcept;
    static void init() noexcept;
    static bool ready() noexcept;
    static void tick() noexcept;
    static void invalidate() noexcept;

    static void term_put(char c) noexcept;
    static void term_clear() noexcept;
    static u32 term_cols() noexcept;
    static u32 term_rows() noexcept;

    static void term_scroll_by(i32 delta) noexcept;
    static void term_scroll_bottom() noexcept;

    static void update_clock() noexcept;

    static void machine_shutdown() noexcept;
    static void machine_restart() noexcept;

    static void set_theme(theme::Id id) noexcept;

    static void game_attach(u32* pixels, u32 width, u32 height, u32 pitch) noexcept;
    static void game_present() noexcept;
    static void game_detach() noexcept;
    static bool game_attached() noexcept;

    // Boot splash: shown once after Compositor::init(). Static logo,
    // animated progress bar. After kBootSplashTicks the desktop appears.
    static void boot_splash_begin() noexcept;
    static bool boot_splash_active() noexcept;
};

} // namespace notyvos::gfx
