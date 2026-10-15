// NotYVOS XMB - the only user-facing program of the OS. Fullscreen, no terminal.
#include "input.hpp"
#include <SDL_image.h>
#include <SDL_ttf.h>
#include <sys/reboot.h>
#include <sys/utsname.h>
#include <unistd.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <map>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace {

constexpr int W = 1920, H = 1080;
constexpr int CAT_Y = 300, CAT_STEP = 230, ANCHOR_X = 400;
constexpr int ITEM_Y = 470, ITEM_STEP = 105;

enum class Act { None, Restart, Shutdown, CycleTheme };

struct Tex { SDL_Texture* t = nullptr; int w = 0, h = 0; };
struct Item { std::string label, sub; Act act = Act::None; };
struct Category { std::string name; std::vector<Item> items; int sel = 0; };
struct ModalOpt { std::string label; Act act; };
struct Modal { bool open = false; std::string title; std::vector<ModalOpt> opts; int sel = 0; };

// One background colour per month, like the original XMB.
const SDL_Color PALETTE[12] = {
    {70,110,190,255},  {150,80,170,255}, {80,170,130,255}, {230,140,170,255},
    {90,190,90,255},   {40,160,200,255}, {230,120,60,255}, {230,200,60,255},
    {110,90,190,255},  {200,120,40,255}, {140,90,60,255},  {190,60,70,255}};

float clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }

std::string slug(const std::string& s) {
    std::string o;
    for (char c : s) if (std::isalnum((unsigned char)c)) o += (char)std::tolower((unsigned char)c);
    return o;
}

std::string envOr(const char* name, const char* def) {
    const char* v = std::getenv(name);
    return v ? v : def;
}

class App {
public:
    bool init();
    void run();
    ~App();

private:
    SDL_Window* win_ = nullptr;
    SDL_Renderer* ren_ = nullptr;
    std::string fontPath_, assetDir_;
    std::map<int, TTF_Font*> fonts_;
    std::map<std::string, Tex> texts_;
    std::map<std::string, SDL_Texture*> icons_;
    std::vector<Category> cats_;
    int cat_ = 0, settingsCat_ = 1, themeItem_ = 0, themeIdx_ = -1;
    float curCat_ = 0, curItem_ = 0, t_ = 0;
    Modal modal_;
    bool running_ = true;
    Uint32 lastMouse_ = 0;

    std::string findFont();
    std::string findAssets();
    void buildMenu();
    void listDir(const std::string& dir, Category& c);
    TTF_Font* font(int px);

    Tex& text(const std::string& s, int px, SDL_Color c);
    void drawText(const std::string& s, int px, SDL_Color c, int x, int y, int anchor, int alpha = 255);
    SDL_Texture* icon(const std::string& slugName);
    void drawIcon(const std::string& name, int cx, int cy, int size, int alpha);
    void fillRect(SDL_Rect r, SDL_Color c, int alpha);
    SDL_Color themeColor() const;
    void drawBackground();
    void render();
    void drawModal();

    void update(float dt);
    void handle(const SDL_Event& e);
    void onPad(Pad p);
    void moveCat(int d);
    void moveItem(int d);
    void activate();
    void perform(Act a);
    void openConfirm(const std::string& title, Act a);
    void cycleTheme();
    void updateThemeSub();
    void click(int x, int y);
    float catX(int i) const { return ANCHOR_X + (i - curCat_) * CAT_STEP; }
    float itemY(int j) const { return ITEM_Y + (j - curItem_) * ITEM_STEP; }
    SDL_Rect modalOptRect(int i) const {
        int n = (int)modal_.opts.size();
        int cx = W / 2 + (int)((i - (n - 1) / 2.0f) * 260);
        return SDL_Rect{cx - 110, H / 2 + 20, 220, 70};
    }
};

std::string App::findFont() {
    std::vector<std::string> dirs = {envOr("NOTYVOS_FONTS", ""), "/usr/share/notyvos/fonts", "fonts"};
    for (auto& d : dirs) {
        if (d.empty()) continue;
        std::error_code ec;
        if (!fs::exists(d, ec)) continue;
        std::vector<std::string> found;
        for (fs::recursive_directory_iterator it(d, fs::directory_options::skip_permission_denied, ec), end;
             !ec && it != end; it.increment(ec)) {
            if (!it->is_regular_file(ec)) continue;
            std::string ext = it->path().extension().string();
            std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
            if (ext == ".ttf" || ext == ".otf") found.push_back(it->path().string());
        }
        if (!found.empty()) { std::sort(found.begin(), found.end()); return found.front(); }
    }
    return "";
}

std::string App::findAssets() {
    std::vector<std::string> dirs = {envOr("NOTYVOS_ASSETS", ""), "/usr/share/notyvos/assets", "assets"};
    for (auto& d : dirs) {
        std::error_code ec;
        if (!d.empty() && fs::exists(d, ec)) return d;
    }
    return "";
}

void App::listDir(const std::string& dir, Category& c) {
    std::error_code ec;
    std::vector<std::string> names;
    for (fs::directory_iterator it(dir, ec), end; !ec && it != end; it.increment(ec))
        names.push_back(it->path().filename().string());
    std::sort(names.begin(), names.end());
    for (size_t i = 0; i < names.size() && i < 300; ++i) c.items.push_back({names[i], "", Act::None});
    if (c.items.empty()) c.items.push_back({"No items", "", Act::None});
}

void App::buildMenu() {
    std::string data = envOr("NOTYVOS_DATA", "/data");
    utsname u{};
    uname(&u);
    int w = 0, h = 0;
    SDL_GetWindowSize(win_, &w, &h);

    cats_.push_back({"Users", {{"Default User", "", Act::None}}, 0});
    Category settings{"Settings", {}, 0};
    settings.items.push_back({"Theme", "", Act::CycleTheme});
    settings.items.push_back({"Display", std::to_string(w) + " x " + std::to_string(h), Act::None});
    settings.items.push_back({"System Information", std::string("NotYVOS - Linux ") + u.release + " " + u.machine, Act::None});
    settings.items.push_back({"Restart System", "", Act::Restart});
    settings.items.push_back({"Shut Down System", "", Act::Shutdown});
    cats_.push_back(settings);
    for (const char* n : {"Photo", "Music", "Video"}) {
        Category c{n, {}, 0};
        listDir(data + "/" + n, c);
        cats_.push_back(c);
    }
    cats_.push_back({"Game", {{"No games installed", "", Act::None}}, 0});
    cats_.push_back({"Network", {{"Network Settings", "Not available yet", Act::None}}, 0});
    cats_.push_back({"PlayStation Network", {{"Sign In", "Not available yet", Act::None}}, 0});
    cats_.push_back({"Friends", {{"No friends", "", Act::None}}, 0});

    settingsCat_ = 1;
    themeItem_ = 0;
    updateThemeSub();
    cat_ = 5;  // start on Game
    curCat_ = (float)cat_;
    curItem_ = 0;
}

bool App::init() {
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "1");
    if (SDL_Init(SDL_INIT_VIDEO) != 0) { std::fprintf(stderr, "SDL: %s\n", SDL_GetError()); return false; }
    if (TTF_Init() != 0) { std::fprintf(stderr, "TTF: %s\n", TTF_GetError()); return false; }
    IMG_Init(IMG_INIT_PNG | IMG_INIT_JPG);

    fontPath_ = findFont();
    if (fontPath_.empty()) { std::fprintf(stderr, "No .ttf/.otf font found in fonts/\n"); return false; }
    assetDir_ = findAssets();

    win_ = SDL_CreateWindow("NotYVOS", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, 1280, 720,
                            SDL_WINDOW_FULLSCREEN_DESKTOP | SDL_WINDOW_ALLOW_HIGHDPI);
    if (!win_) { std::fprintf(stderr, "Window: %s\n", SDL_GetError()); return false; }
    ren_ = SDL_CreateRenderer(win_, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!ren_) ren_ = SDL_CreateRenderer(win_, -1, SDL_RENDERER_SOFTWARE);
    if (!ren_) { std::fprintf(stderr, "Renderer: %s\n", SDL_GetError()); return false; }
    SDL_RenderSetLogicalSize(ren_, W, H);
    SDL_SetRenderDrawBlendMode(ren_, SDL_BLENDMODE_BLEND);
    buildMenu();
    return true;
}

App::~App() {
    for (auto& kv : texts_) if (kv.second.t) SDL_DestroyTexture(kv.second.t);
    for (auto& kv : icons_) if (kv.second) SDL_DestroyTexture(kv.second);
    for (auto& kv : fonts_) if (kv.second) TTF_CloseFont(kv.second);
    if (ren_) SDL_DestroyRenderer(ren_);
    if (win_) SDL_DestroyWindow(win_);
    IMG_Quit(); TTF_Quit(); SDL_Quit();
}

TTF_Font* App::font(int px) {
    auto it = fonts_.find(px);
    if (it != fonts_.end()) return it->second;
    TTF_Font* f = TTF_OpenFont(fontPath_.c_str(), px);
    fonts_[px] = f;
    return f;
}

Tex& App::text(const std::string& s, int px, SDL_Color c) {
    char key[32];
    std::snprintf(key, sizeof key, "%d:%02x%02x%02x:", px, c.r, c.g, c.b);
    std::string k = std::string(key) + s;
    auto it = texts_.find(k);
    if (it != texts_.end()) return it->second;
    if (texts_.size() > 600) {
        for (auto& kv : texts_) if (kv.second.t) SDL_DestroyTexture(kv.second.t);
        texts_.clear();
    }
    Tex t;
    if (TTF_Font* f = font(px)) {
        if (SDL_Surface* sf = TTF_RenderUTF8_Blended(f, s.c_str(), c)) {
            t.t = SDL_CreateTextureFromSurface(ren_, sf);
            t.w = sf->w; t.h = sf->h;
            SDL_FreeSurface(sf);
        }
    }
    return texts_[k] = t;
}

void App::drawText(const std::string& s, int px, SDL_Color c, int x, int y, int anchor, int alpha) {
    Tex& t = text(s, px, c);
    if (!t.t) return;
    SDL_SetTextureAlphaMod(t.t, (Uint8)clampf((float)alpha, 0, 255));
    int dx = anchor == 0 ? x : (anchor == 1 ? x - t.w / 2 : x - t.w);
    SDL_Rect r{dx, y - t.h / 2, t.w, t.h};
    SDL_RenderCopy(ren_, t.t, nullptr, &r);
}

SDL_Texture* App::icon(const std::string& s) {
    auto it = icons_.find(s);
    if (it != icons_.end()) return it->second;
    SDL_Texture* t = nullptr;
    if (!assetDir_.empty()) t = IMG_LoadTexture(ren_, (assetDir_ + "/icons/" + s + ".png").c_str());
    icons_[s] = t;
    return t;
}

void App::fillRect(SDL_Rect r, SDL_Color c, int alpha) {
    SDL_SetRenderDrawColor(ren_, c.r, c.g, c.b, (Uint8)clampf((float)alpha, 0, 255));
    SDL_RenderFillRect(ren_, &r);
}

void App::drawIcon(const std::string& name, int cx, int cy, int size, int alpha) {
    SDL_Rect r{cx - size / 2, cy - size / 2, size, size};
    if (SDL_Texture* t = icon(slug(name))) {
        SDL_SetTextureAlphaMod(t, (Uint8)clampf((float)alpha, 0, 255));
        SDL_RenderCopy(ren_, t, nullptr, &r);
        return;
    }
    // Fallback glyph tile when assets/icons/<name>.png is missing.
    fillRect(r, {255, 255, 255, 255}, alpha / 6);
    SDL_SetRenderDrawColor(ren_, 255, 255, 255, (Uint8)(alpha * 0.7));
    SDL_RenderDrawRect(ren_, &r);
    drawText(name.substr(0, 1), size / 2, {255, 255, 255, 255}, cx, cy, 1, alpha);
}

SDL_Color App::themeColor() const {
    int idx = themeIdx_;
    if (idx < 0) {
        std::time_t now = std::time(nullptr);
        std::tm tm{};
        localtime_r(&now, &tm);
        idx = tm.tm_mon;
    }
    return PALETTE[idx % 12];
}

void App::drawBackground() {
    SDL_Color top = themeColor();
    SDL_Color bot{(Uint8)(top.r / 4), (Uint8)(top.g / 4), (Uint8)(top.b / 4), 255};
    SDL_Vertex v[4] = {
        {{0.f, 0.f}, top, {0, 0}}, {{(float)W, 0.f}, top, {0, 0}},
        {{(float)W, (float)H}, bot, {0, 0}}, {{0.f, (float)H}, bot, {0, 0}}};
    int idx[6] = {0, 1, 2, 0, 2, 3};
    SDL_RenderGeometry(ren_, nullptr, v, 4, idx, 6);

    const int step = 48, n = W / step + 2;
    for (int r = 0; r < 3; ++r) {
        std::vector<SDL_Vertex> vs;
        std::vector<int> ix;
        float ph = t_ * (0.35f + 0.12f * r) + r * 1.7f;
        Uint8 a = (Uint8)(34 - r * 8);
        for (int i = 0; i < n; ++i) {
            float x = (float)(i * step);
            float yc = H * (0.55f + 0.12f * r) + 70 * std::sin(x * 0.0035f * (1 + 0.3f * r) + ph) +
                       40 * std::sin(x * 0.0081f - ph * 1.3f);
            float th = 90 + 40 * std::sin(x * 0.004f + ph * 0.7f) + r * 20;
            vs.push_back({{x, yc - th * 0.5f}, {255, 255, 255, 0}, {0, 0}});
            vs.push_back({{x, yc}, {255, 255, 255, a}, {0, 0}});
            vs.push_back({{x, yc + th * 0.5f}, {255, 255, 255, 0}, {0, 0}});
        }
        for (int i = 0; i + 1 < n; ++i) {
            int b = i * 3;
            int q[12] = {b, b + 1, b + 3, b + 1, b + 4, b + 3, b + 1, b + 2, b + 4, b + 2, b + 5, b + 4};
            ix.insert(ix.end(), q, q + 12);
        }
        SDL_RenderGeometry(ren_, nullptr, vs.data(), (int)vs.size(), ix.data(), (int)ix.size());
    }
}

void App::render() {
    drawBackground();
    const SDL_Color white{255, 255, 255, 255};

    char buf[64];
    std::time_t now = std::time(nullptr);
    std::tm tm{};
    localtime_r(&now, &tm);
    std::strftime(buf, sizeof buf, "%a %m/%d  %H:%M", &tm);
    drawText(buf, 34, white, W - 80, 70, 2, 230);

    for (int i = 0; i < (int)cats_.size(); ++i) {
        float d = std::fabs(i - curCat_);
        int a = (int)clampf(255 - d * 70, 0, 255);
        if (a <= 0) continue;
        int size = (int)(128 - 40 * clampf(d, 0, 1));
        int cx = (int)catX(i);
        drawIcon(cats_[i].name, cx, CAT_Y, size, a);
        if (d < 0.7f) drawText(cats_[i].name, 30, white, cx, CAT_Y + 100, 1, (int)(a * (1 - d / 0.7f)));
    }

    Category& c = cats_[cat_];
    float ix = catX(cat_);
    for (int j = 0; j < (int)c.items.size(); ++j) {
        float d = std::fabs(j - curItem_);
        float y = itemY(j);
        if (y < 380 || y > H + 60) continue;
        float a = clampf(255 - d * 45, 40, 255);
        if (y < 450) a *= clampf((y - 380) / 70.f, 0, 1);
        bool selected = d < 0.5f;
        drawIcon(c.items[j].label, (int)ix, (int)y, selected ? 84 : 64, (int)a);
        drawText(c.items[j].label, selected ? 40 : 32, white, (int)ix + 70,
                 (int)y - (selected && !c.items[j].sub.empty() ? 14 : 0), 0, (int)a);
        if (selected && !c.items[j].sub.empty())
            drawText(c.items[j].sub, 26, white, (int)ix + 70, (int)y + 26, 0, (int)(a * 0.75f));
    }

    if (modal_.open) drawModal();

    if (t_ < 2.2f) {  // boot fade-in
        float k = t_ < 1.2f ? 1.f : 1.f - (t_ - 1.2f) / 1.0f;
        fillRect({0, 0, W, H}, {0, 0, 0, 255}, (int)(255 * k));
        drawText("NotYVOS", 90, white, W / 2, H / 2, 1, (int)(255 * k));
    }
}

void App::drawModal() {
    const SDL_Color white{255, 255, 255, 255};
    fillRect({0, 0, W, H}, {0, 0, 0, 255}, 150);
    fillRect({W / 2 - 440, H / 2 - 140, 880, 300}, {20, 20, 30, 255}, 235);
    drawText(modal_.title, 44, white, W / 2, H / 2 - 60, 1);
    for (int i = 0; i < (int)modal_.opts.size(); ++i) {
        SDL_Rect r = modalOptRect(i);
        bool s = i == modal_.sel;
        fillRect(r, {255, 255, 255, 255}, s ? 70 : 15);
        drawText(modal_.opts[i].label, 32, white, r.x + r.w / 2, r.y + r.h / 2, 1, s ? 255 : 160);
    }
}

void App::update(float dt) {
    float k = std::min(1.f, dt * 12.f);
    curCat_ += ((float)cat_ - curCat_) * k;
    curItem_ += ((float)cats_[cat_].sel - curItem_) * k;
    if (lastMouse_ && SDL_GetTicks() - lastMouse_ > 3000) { SDL_ShowCursor(SDL_DISABLE); lastMouse_ = 0; }
}

void App::moveCat(int d) {
    int n = (int)cats_.size();
    int nc = std::max(0, std::min(n - 1, cat_ + d));
    if (nc == cat_) return;
    cat_ = nc;
    curItem_ = (float)cats_[cat_].sel;
}

void App::moveItem(int d) {
    Category& c = cats_[cat_];
    c.sel = std::max(0, std::min((int)c.items.size() - 1, c.sel + d));
}

void App::cycleTheme() {
    if (++themeIdx_ > 11) themeIdx_ = -1;
    updateThemeSub();
}

void App::updateThemeSub() {
    cats_[settingsCat_].items[themeItem_].sub =
        themeIdx_ < 0 ? "Automatic (changes with the month)" : "Color " + std::to_string(themeIdx_ + 1);
}

void App::openConfirm(const std::string& title, Act a) {
    modal_ = {true, title, {{"Yes", a}, {"No", Act::None}}, 1};
}

void App::perform(Act a) {
    if (a != Act::Restart && a != Act::Shutdown) return;
    // Only touch the machine when running as the OS session (set by notyvos-session).
    if (!std::getenv("NOTYVOS_APPLIANCE")) { running_ = false; return; }
    sync();
    if (reboot(a == Act::Restart ? RB_AUTOBOOT : RB_POWER_OFF) != 0) running_ = false;
}

void App::activate() {
    Act a = cats_[cat_].items[cats_[cat_].sel].act;
    if (a == Act::CycleTheme) cycleTheme();
    else if (a == Act::Restart) openConfirm("Restart the system?", a);
    else if (a == Act::Shutdown) openConfirm("Shut down the system?", a);
}

void App::onPad(Pad p) {
    if (modal_.open) {
        int n = (int)modal_.opts.size();
        switch (p) {
            case Pad::LS_Left: case Pad::DP_Left: case Pad::RS_Left: modal_.sel = std::max(0, modal_.sel - 1); break;
            case Pad::LS_Right: case Pad::DP_Right: case Pad::RS_Right: modal_.sel = std::min(n - 1, modal_.sel + 1); break;
            case Pad::Cross: { Act a = modal_.opts[modal_.sel].act; modal_.open = false; perform(a); break; }
            case Pad::Circle: case Pad::PS: modal_.open = false; break;
            default: break;
        }
        return;
    }
    switch (p) {
        case Pad::LS_Left: case Pad::RS_Left: case Pad::DP_Left: case Pad::L1: moveCat(-1); break;
        case Pad::LS_Right: case Pad::RS_Right: case Pad::DP_Right: case Pad::R1: moveCat(1); break;
        case Pad::LS_Up: case Pad::RS_Up: case Pad::DP_Up: moveItem(-1); break;
        case Pad::LS_Down: case Pad::RS_Down: case Pad::DP_Down: moveItem(1); break;
        case Pad::L2: moveItem(-5); break;
        case Pad::R2: moveItem(5); break;
        case Pad::Cross: activate(); break;
        case Pad::Triangle: cats_[cat_].sel = 0; break;
        case Pad::Square: cycleTheme(); break;
        case Pad::Start: cat_ = settingsCat_; curItem_ = (float)cats_[cat_].sel; break;
        case Pad::PS:
            modal_ = {true, "Quit Game / System", {{"Restart", Act::Restart}, {"Shut Down", Act::Shutdown}, {"Cancel", Act::None}}, 2};
            break;
        default: break;  // Select, Circle, L3, R3 are reserved.
    }
}

void App::click(int x, int y) {
    if (modal_.open) {
        for (int i = 0; i < (int)modal_.opts.size(); ++i) {
            SDL_Rect r = modalOptRect(i);
            if (x >= r.x && x < r.x + r.w && y >= r.y && y < r.y + r.h) {
                Act a = modal_.opts[i].act;
                modal_.open = false;
                perform(a);
                return;
            }
        }
        return;
    }
    if (std::abs(y - CAT_Y) < 110) {
        for (int i = 0; i < (int)cats_.size(); ++i)
            if (std::fabs(x - catX(i)) < 110) { cat_ = i; curItem_ = (float)cats_[i].sel; return; }
        return;
    }
    Category& c = cats_[cat_];
    for (int j = 0; j < (int)c.items.size(); ++j)
        if (std::fabs(y - itemY(j)) < 50 && x > catX(cat_) - 60 && x < catX(cat_) + 900) {
            c.sel = j;
            activate();
            return;
        }
}

void App::handle(const SDL_Event& e) {
    switch (e.type) {
        case SDL_QUIT: running_ = false; break;
        case SDL_KEYDOWN: {
            Pad p;
            if (!mapKey(e.key.keysym.scancode, p)) break;
            if (e.key.repeat && !isDirectional(p)) break;
            onPad(p);
            break;
        }
        case SDL_MOUSEMOTION:
            SDL_ShowCursor(SDL_ENABLE);
            lastMouse_ = SDL_GetTicks();
            if (!modal_.open && std::abs(e.motion.y - CAT_Y) >= 110) {
                Category& c = cats_[cat_];
                for (int j = 0; j < (int)c.items.size(); ++j)
                    if (std::fabs(e.motion.y - itemY(j)) < 50 && e.motion.x > catX(cat_) - 60 && e.motion.x < catX(cat_) + 900)
                        c.sel = j;
            }
            break;
        case SDL_MOUSEBUTTONDOWN:
            lastMouse_ = SDL_GetTicks();
            if (e.button.button == SDL_BUTTON_LEFT) click(e.button.x, e.button.y);
            else if (e.button.button == SDL_BUTTON_RIGHT && modal_.open) modal_.open = false;
            break;
        case SDL_MOUSEWHEEL: {
            bool horiz = e.wheel.x != 0 || (SDL_GetModState() & KMOD_SHIFT);
            int d = e.wheel.x != 0 ? e.wheel.x : e.wheel.y;
            if (modal_.open) break;
            if (horiz) moveCat(d > 0 ? 1 : -1);
            else moveItem(e.wheel.y > 0 ? -1 : 1);
            break;
        }
        default: break;
    }
}

void App::run() {
    Uint32 last = SDL_GetTicks();
    while (running_) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) handle(e);
        Uint32 now = SDL_GetTicks();
        float dt = (now - last) / 1000.f;
        last = now;
        t_ += dt;
        update(dt);
        render();
        SDL_RenderPresent(ren_);
        SDL_Delay(2);
    }
}

}  // namespace

int main() {
    App app;
    if (!app.init()) return 1;
    app.run();
    return 0;
}
