#include <kernel/gfx/widget.hpp>
#include <kernel/libk/string.hpp>

namespace notyvos::gfx
{

namespace
{

WidgetRectFn g_rect = nullptr;
WidgetTextFn g_text = nullptr;

void dr(i32 x, i32 y, i32 w, i32 h, u32 c) noexcept
{
    if (g_rect)
        g_rect(x, y, w, h, c);
}
void dt(i32 x, i32 y, const char* s, u32 fg, u32 bg) noexcept
{
    if (g_text)
        g_text(x, y, s, fg, bg);
}

constexpr u32 kBtnBg = 0x00305070;
constexpr u32 kBtnHover = 0x00406890;
constexpr u32 kBtnPress = 0x00203848;
constexpr u32 kBtnEdge = 0x00586078;
constexpr u32 kBtnFg = 0x00E8E8E8;
constexpr u32 kListBg = 0x00181820;
constexpr u32 kListHov = 0x00406080;
constexpr u32 kListSel = 0x006090C0;
constexpr u32 kListFg = 0x00E0E0E0;
constexpr u32 kLabelFg = 0x00E0E0E0;
constexpr u32 kCellW = 16;
constexpr u32 kCellH = 16;

} // namespace

void widget_bind(WidgetRectFn rf, WidgetTextFn tf) noexcept
{
    g_rect = rf;
    g_text = tf;
}

// ---- Button ----

Button::Button() noexcept
    : label_(""), rect_{0, 0, 0, 0}, cb_(nullptr), user_(nullptr), state_(WidgetState::Normal)
{
}

Button::Button(const char* label, Rect r, ButtonCallback cb, void* user) noexcept
    : label_(label ? label : ""), rect_(r), cb_(cb), user_(user), state_(WidgetState::Normal)
{
}

void Button::init(const char* label, Rect r, ButtonCallback cb, void* user) noexcept
{
    label_ = label ? label : "";
    rect_ = r;
    cb_ = cb;
    user_ = user;
    state_ = WidgetState::Normal;
}

void Button::draw(i32 mx, i32 my, bool /*mouse_down*/) const noexcept
{
    const bool hover = rect_.contains(mx, my);
    const u32 bg = (state_ == WidgetState::Pressed) ? kBtnPress : hover ? kBtnHover : kBtnBg;
    dr(rect_.x, rect_.y, rect_.w, rect_.h, bg);
    dr(rect_.x, rect_.y, rect_.w, 1, kBtnEdge);
    dr(rect_.x, rect_.y + rect_.h - 1, rect_.w, 1, kBtnEdge);
    dr(rect_.x, rect_.y, 1, rect_.h, kBtnEdge);
    dr(rect_.x + rect_.w - 1, rect_.y, 1, rect_.h, kBtnEdge);

    const u32 len = static_cast<u32>(libk::strlen(label_));
    const i32 tw = static_cast<i32>(len) * static_cast<i32>(kCellW);
    const i32 tx = rect_.x + (rect_.w - tw) / 2;
    const i32 ty = rect_.y + (rect_.h - static_cast<i32>(kCellH)) / 2;
    dt(tx, ty, label_, kBtnFg, bg);
}

bool Button::on_click(i32 mx, i32 my, bool pressed_edge) noexcept
{
    if (!pressed_edge)
    {
        if (state_ == WidgetState::Pressed)
            state_ = WidgetState::Normal;
        return false;
    }
    if (rect_.contains(mx, my))
    {
        state_ = WidgetState::Pressed;
        if (cb_)
            cb_(user_);
        return true;
    }
    return false;
}

// ---- Label ----

Label::Label() noexcept : text_(""), x_(0), y_(0), fg_(kLabelFg) {}

Label::Label(const char* text, i32 x, i32 y, u32 fg) noexcept
    : text_(text ? text : ""), x_(x), y_(y), fg_(fg ? fg : kLabelFg)
{
}

void Label::init(const char* text, i32 x, i32 y, u32 fg) noexcept
{
    text_ = text ? text : "";
    x_ = x;
    y_ = y;
    fg_ = fg ? fg : kLabelFg;
}

void Label::draw() const noexcept
{
    dt(x_, y_, text_, fg_, 0x00000000);
}

// ---- ListView ----

ListView::ListView() noexcept
    : rect_{0, 0, 0, 0}, count_(0), hovered_(-1), selected_(-1), scroll_(0)
{
    for (u32 i = 0; i < kMaxItems; ++i)
        items_[i] = "";
}

void ListView::init(Rect r) noexcept
{
    rect_ = r;
}

void ListView::add(const char* item) noexcept
{
    if (count_ >= kMaxItems)
        return;
    items_[count_++] = item ? item : "";
}

void ListView::clear() noexcept
{
    count_ = 0;
    hovered_ = -1;
    selected_ = -1;
    scroll_ = 0;
}

void ListView::draw(i32 mx, i32 my) noexcept
{
    dr(rect_.x, rect_.y, rect_.w, rect_.h, kListBg);

    hovered_ = -1;
    for (u32 i = 0; i < count_; ++i)
    {
        const i32 row_y = rect_.y + 2 + static_cast<i32>(i) * static_cast<i32>(kCellH + 2);
        if (row_y + static_cast<i32>(kCellH) > rect_.y + rect_.h)
            break;

        const bool hover = (mx >= rect_.x + 2 && mx < rect_.x + rect_.w - 2 && my >= row_y &&
                            my < row_y + static_cast<i32>(kCellH));
        if (hover)
            hovered_ = static_cast<i32>(i);

        u32 bg = kListBg;
        if (static_cast<i32>(i) == selected_)
            bg = kListSel;
        else if (hover)
            bg = kListHov;

        if (bg != kListBg)
        {
            dr(rect_.x + 1, row_y, rect_.w - 2, static_cast<i32>(kCellH), bg);
        }
        dt(rect_.x + 6, row_y, items_[i], kListFg, bg);
    }
}

bool ListView::on_click(i32 mx, i32 my, bool pressed_edge) noexcept
{
    if (!pressed_edge)
        return false;
    if (!rect_.contains(mx, my))
        return false;
    if (hovered_ >= 0)
    {
        selected_ = hovered_;
        return true;
    }
    return false;
}

} // namespace notyvos::gfx
