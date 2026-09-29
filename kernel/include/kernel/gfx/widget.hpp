#pragma once
#include <kernel/types.hpp>

namespace notyvos::gfx
{

enum class WidgetState : u8
{
    Normal,
    Hover,
    Pressed
};

struct Rect
{
    i32 x, y, w, h;
    bool contains(i32 mx, i32 my) const noexcept
    {
        return mx >= x && mx < x + w && my >= y && my < y + h;
    }
};

using ButtonCallback = void (*)(void* user);
using WidgetRectFn = void (*)(i32 x, i32 y, i32 w, i32 h, u32 color);
using WidgetTextFn = void (*)(i32 x, i32 y, const char* s, u32 fg, u32 bg);

// Compositor registers the drawing primitives once at init.
void widget_bind(WidgetRectFn rect_fn, WidgetTextFn text_fn) noexcept;

class Button
{
public:
    Button() noexcept;
    Button(const char* label, Rect r, ButtonCallback cb, void* user = nullptr) noexcept;
    void init(const char* label, Rect r, ButtonCallback cb, void* user = nullptr) noexcept;
    void draw(i32 mx, i32 my, bool mouse_down) const noexcept;
    bool on_click(i32 mx, i32 my, bool pressed_edge) noexcept;
    const char* label() const noexcept
    {
        return label_;
    }
    Rect bounds() const noexcept
    {
        return rect_;
    }

private:
    const char* label_;
    Rect rect_;
    ButtonCallback cb_;
    void* user_;
    WidgetState state_;
};

class Label
{
public:
    Label() noexcept;
    Label(const char* text, i32 x, i32 y, u32 fg = 0x00E0E0E0) noexcept;
    void init(const char* text, i32 x, i32 y, u32 fg = 0x00E0E0E0) noexcept;
    void set_text(const char* t) noexcept
    {
        text_ = t ? t : "";
    }
    void set_pos(i32 x, i32 y) noexcept
    {
        x_ = x;
        y_ = y;
    }
    void draw() const noexcept;

private:
    const char* text_;
    i32 x_, y_;
    u32 fg_;
};

class ListView
{
public:
    static constexpr u32 kMaxItems = 32;

    ListView() noexcept;
    void init(Rect r) noexcept;
    void add(const char* item) noexcept;
    void clear() noexcept;
    void draw(i32 mx, i32 my) noexcept;
    i32 hovered() const noexcept
    {
        return hovered_;
    }
    i32 selected() const noexcept
    {
        return selected_;
    }
    bool on_click(i32 mx, i32 my, bool pressed_edge) noexcept;

private:
    Rect rect_;
    const char* items_[kMaxItems];
    u32 count_;
    i32 hovered_;
    i32 selected_;
    u32 scroll_;
};

} // namespace notyvos::gfx
