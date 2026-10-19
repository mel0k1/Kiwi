#include "gfx_win.hpp"

#include <algorithm>
#include <objidl.h>
#include <gdiplus.h>

#include <cmath>
#include <cstdio>
#include <cstring>
#include <memory>
#include <vector>

namespace kiwi {
namespace gfx {

namespace {

ULONG_PTR g_token = 0;
constexpr float kPi = 3.14159265f;
bool g_dark_chrome = false;

// Theme slots (refilled in place by apply_named_theme) and the cached
// WM_CTLCOLOR brushes built from the aux slots.
Palette g_pal_light;
Palette g_pal_dark;
AuxTheme g_aux_light;
AuxTheme g_aux_dark;
HBRUSH g_surface_brush_light = nullptr;
HBRUSH g_surface_brush_dark = nullptr;
HBRUSH g_field_brush_light = nullptr;
HBRUSH g_field_brush_dark = nullptr;

Palette builtin_light_palette() {
    Palette p;
    p.chrome_bg = RGB(243, 243, 246);
    p.chrome_text = RGB(32, 33, 36);
    p.chrome_muted = RGB(110, 116, 124);
    p.active_tab_bg = RGB(255, 255, 255);
    p.hover_chip = RGB(231, 232, 237);
    p.pressed_chip = RGB(222, 223, 229);
    p.outline = RGB(214, 216, 221);
    p.omnibox_bg = RGB(255, 255, 255);
    p.omnibox_border = RGB(206, 209, 215);
    p.omnibox_focus = RGB(224, 128, 60);
    p.accent = RGB(224, 128, 60);
    p.separator = RGB(220, 222, 227);
    p.content_bg = RGB(255, 255, 255);
    p.danger_hover = RGB(242, 139, 130);
    return p;
}

Palette builtin_dark_palette() {
    Palette p;
    p.chrome_bg = RGB(32, 33, 36);
    p.chrome_text = RGB(226, 228, 232);
    p.chrome_muted = RGB(154, 160, 166);
    p.active_tab_bg = RGB(54, 57, 63);
    p.hover_chip = RGB(44, 47, 52);
    p.pressed_chip = RGB(50, 54, 59);
    p.outline = RGB(60, 64, 67);
    p.omnibox_bg = RGB(42, 45, 49);
    p.omnibox_border = RGB(68, 72, 77);
    p.omnibox_focus = RGB(224, 128, 60);
    p.accent = RGB(224, 128, 60);
    p.separator = RGB(52, 55, 60);
    p.content_bg = RGB(32, 33, 36);
    p.danger_hover = RGB(197, 87, 77);
    return p;
}

AuxTheme builtin_light_aux() {
    AuxTheme t;
    t.surface = RGB(245, 246, 249);
    t.ink = RGB(32, 33, 36);
    t.muted = RGB(110, 116, 124);
    t.field_bg = RGB(255, 255, 255);
    t.outline = RGB(206, 209, 215);
    return t;
}

AuxTheme builtin_dark_aux() {
    AuxTheme t;
    t.surface = RGB(26, 27, 30);
    t.ink = RGB(226, 228, 232);
    t.muted = RGB(154, 160, 166);
    t.field_bg = RGB(36, 38, 41);
    t.outline = RGB(60, 64, 67);
    return t;
}

// A ready-made theme: one chrome palette + the matching dialog theme. The
// palette fills BOTH slots while the theme is active (palette() picks by the
// dark flag, which the theme also carries).
struct ThemeSpec {
    const char* name;
    bool dark;
    Palette palette;
    AuxTheme aux;
};

const std::vector<ThemeSpec>& theme_table() {
    static const std::vector<ThemeSpec> table = []() {
        std::vector<ThemeSpec> t;
        {
            Palette p; AuxTheme a;
            // Nord (Polar Night / Snow Storm, frost accent)
            p.chrome_bg = RGB(46, 52, 64);       p.chrome_text = RGB(236, 239, 244);
            p.chrome_muted = RGB(167, 177, 194); p.active_tab_bg = RGB(59, 66, 82);
            p.hover_chip = RGB(53, 60, 72);      p.pressed_chip = RGB(62, 70, 84);
            p.outline = RGB(67, 76, 94);         p.omnibox_bg = RGB(51, 58, 70);
            p.omnibox_border = RGB(76, 85, 104); p.omnibox_focus = RGB(136, 192, 208);
            p.accent = RGB(136, 192, 208);       p.separator = RGB(58, 66, 80);
            p.content_bg = RGB(46, 52, 64);      p.danger_hover = RGB(191, 97, 106);
            a.surface = RGB(41, 46, 56);         a.ink = RGB(236, 239, 244);
            a.muted = RGB(167, 177, 194);        a.field_bg = RGB(53, 60, 72);
            a.outline = RGB(67, 76, 94);
            t.push_back({"nord", true, p, a});
        }
        {
            Palette p; AuxTheme a;
            // Dracula (dark purple, pink/purple accents)
            p.chrome_bg = RGB(40, 42, 54);       p.chrome_text = RGB(248, 248, 242);
            p.chrome_muted = RGB(157, 162, 196); p.active_tab_bg = RGB(52, 55, 70);
            p.hover_chip = RGB(47, 49, 64);      p.pressed_chip = RGB(56, 59, 76);
            p.outline = RGB(68, 71, 90);         p.omnibox_bg = RGB(46, 48, 62);
            p.omnibox_border = RGB(74, 77, 98);  p.omnibox_focus = RGB(189, 147, 249);
            p.accent = RGB(189, 147, 249);       p.separator = RGB(58, 61, 78);
            p.content_bg = RGB(40, 42, 54);      p.danger_hover = RGB(255, 85, 85);
            a.surface = RGB(35, 37, 48);         a.ink = RGB(248, 248, 242);
            a.muted = RGB(157, 162, 196);        a.field_bg = RGB(52, 55, 70);
            a.outline = RGB(68, 71, 90);
            t.push_back({"dracula", true, p, a});
        }
        {
            Palette p; AuxTheme a;
            // Catppuccin Mocha (soft dark, blue accent)
            p.chrome_bg = RGB(30, 30, 46);       p.chrome_text = RGB(205, 214, 244);
            p.chrome_muted = RGB(147, 153, 178); p.active_tab_bg = RGB(49, 50, 68);
            p.hover_chip = RGB(42, 43, 60);      p.pressed_chip = RGB(52, 53, 74);
            p.outline = RGB(69, 71, 90);         p.omnibox_bg = RGB(38, 38, 55);
            p.omnibox_border = RGB(78, 80, 100); p.omnibox_focus = RGB(137, 180, 250);
            p.accent = RGB(137, 180, 250);       p.separator = RGB(49, 50, 68);
            p.content_bg = RGB(30, 30, 46);      p.danger_hover = RGB(243, 139, 168);
            a.surface = RGB(24, 24, 37);         a.ink = RGB(205, 214, 244);
            a.muted = RGB(147, 153, 178);        a.field_bg = RGB(49, 50, 68);
            a.outline = RGB(69, 71, 90);
            t.push_back({"catppuccin", true, p, a});
        }
        {
            Palette p; AuxTheme a;
            // Solarized Light (warm paper, blue accent)
            p.chrome_bg = RGB(253, 246, 227);    p.chrome_text = RGB(7, 54, 66);
            p.chrome_muted = RGB(147, 161, 161); p.active_tab_bg = RGB(238, 232, 213);
            p.hover_chip = RGB(234, 227, 206);   p.pressed_chip = RGB(226, 218, 192);
            p.outline = RGB(221, 214, 195);      p.omnibox_bg = RGB(255, 254, 246);
            p.omnibox_border = RGB(213, 205, 184); p.omnibox_focus = RGB(38, 139, 210);
            p.accent = RGB(38, 139, 210);        p.separator = RGB(228, 220, 198);
            p.content_bg = RGB(253, 246, 227);   p.danger_hover = RGB(220, 50, 47);
            a.surface = RGB(245, 239, 220);      a.ink = RGB(7, 54, 66);
            a.muted = RGB(147, 161, 161);        a.field_bg = RGB(255, 254, 246);
            a.outline = RGB(213, 205, 184);
            t.push_back({"solarized", false, p, a});
        }
        {
            Palette p; AuxTheme a;
            // Sakura (pastel pink paper, rose accent)
            p.chrome_bg = RGB(250, 237, 242);    p.chrome_text = RGB(92, 62, 74);
            p.chrome_muted = RGB(168, 130, 146); p.active_tab_bg = RGB(255, 250, 252);
            p.hover_chip = RGB(245, 224, 232);   p.pressed_chip = RGB(238, 211, 222);
            p.outline = RGB(235, 209, 220);      p.omnibox_bg = RGB(255, 252, 253);
            p.omnibox_border = RGB(233, 205, 216); p.omnibox_focus = RGB(228, 115, 154);
            p.accent = RGB(228, 115, 154);       p.separator = RGB(240, 219, 228);
            p.content_bg = RGB(255, 251, 252);   p.danger_hover = RGB(214, 78, 110);
            a.surface = RGB(247, 232, 238);      a.ink = RGB(92, 62, 74);
            a.muted = RGB(168, 130, 146);        a.field_bg = RGB(255, 252, 253);
            a.outline = RGB(233, 205, 216);
            t.push_back({"sakura", false, p, a});
        }
        {
            Palette p; AuxTheme a;
            // Crimson (warm near-black, red accent)
            p.chrome_bg = RGB(24, 20, 21);       p.chrome_text = RGB(235, 226, 226);
            p.chrome_muted = RGB(168, 148, 150); p.active_tab_bg = RGB(43, 33, 35);
            p.hover_chip = RGB(36, 27, 29);      p.pressed_chip = RGB(51, 39, 41);
            p.outline = RGB(66, 49, 52);         p.omnibox_bg = RGB(33, 25, 27);
            p.omnibox_border = RGB(72, 52, 56);  p.omnibox_focus = RGB(230, 82, 88);
            p.accent = RGB(230, 82, 88);         p.separator = RGB(45, 34, 36);
            p.content_bg = RGB(24, 20, 21);      p.danger_hover = RGB(255, 107, 112);
            a.surface = RGB(18, 14, 15);         a.ink = RGB(235, 226, 226);
            a.muted = RGB(168, 148, 150);        a.field_bg = RGB(36, 27, 29);
            a.outline = RGB(72, 52, 56);
            t.push_back({"crimson", true, p, a});
        }
        return t;
    }();
    return table;
}

const ThemeSpec* find_theme(const std::string& name) {
    for (const ThemeSpec& spec : theme_table()) {
        if (name == spec.name) return &spec;
    }
    return nullptr;
}

// "#RRGGBB" -> COLORREF; CLR_INVALID when the string is not valid hex.
COLORREF parse_hex_color(const std::string& hex) {
    if (hex.size() != 7 || hex[0] != '#') return CLR_INVALID;
    auto digit = [&](char c) -> int {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        return -1;
    };
    int hi[3] = {digit(hex[1]), digit(hex[3]), digit(hex[5])};
    int lo[3] = {digit(hex[2]), digit(hex[4]), digit(hex[6])};
    for (int i = 0; i < 3; ++i) {
        if (hi[i] < 0 || lo[i] < 0) return CLR_INVALID;
    }
    return RGB(hi[0] * 16 + lo[0], hi[1] * 16 + lo[1], hi[2] * 16 + lo[2]);
}

void refresh_aux_brushes() {
    // Fresh brushes each time. The previous brushes are orphaned, not
    // deleted: window-class backgrounds (registered with the brush that
    // existed at registration time) and in-flight WM_CTLCOLOR replies may
    // still reference them, and deleting a brush under such a reference is
    // undefined behavior. Orphaned solid brushes cost a few GDI bytes each
    // and theme changes are rare user actions, so the bounded leak is the
    // safe side of the trade.
    auto replace = [](HBRUSH& brush, COLORREF color) {
        HBRUSH fresh = CreateSolidBrush(color);
        brush = fresh;
    };
    replace(g_surface_brush_light, g_aux_light.surface);
    replace(g_surface_brush_dark, g_aux_dark.surface);
    replace(g_field_brush_light, g_aux_light.field_bg);
    replace(g_field_brush_dark, g_aux_dark.field_bg);
}

Gdiplus::Graphics* graphics_on(HDC dc) {
    Gdiplus::Graphics* g = new Gdiplus::Graphics(dc);
    g->SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
    g->SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
    g->SetPixelOffsetMode(Gdiplus::PixelOffsetModeHalf);
    return g;
}

Gdiplus::Color make_color(COLORREF color) {
    return Gdiplus::Color(255, GetRValue(color), GetGValue(color),
                          GetBValue(color));
}

std::unique_ptr<Gdiplus::Pen> make_pen(COLORREF color, float width) {
    auto pen = std::make_unique<Gdiplus::Pen>(make_color(color), width);
    pen->SetStartCap(Gdiplus::LineCapRound);
    pen->SetEndCap(Gdiplus::LineCapRound);
    pen->SetLineJoin(Gdiplus::LineJoinRound);
    return pen;
}

Gdiplus::GraphicsPath* round_path(const Gdiplus::RectF& rect, float radius) {
    auto* path = new Gdiplus::GraphicsPath();
    float d = radius * 2.0f;
    path->AddArc(rect.X, rect.Y, d, d, 180.0f, 90.0f);
    path->AddArc(rect.X + rect.Width - d, rect.Y, d, d, 270.0f, 90.0f);
    path->AddArc(rect.X + rect.Width - d, rect.Y + rect.Height - d, d, d,
                 0.0f, 90.0f);
    path->AddArc(rect.X, rect.Y + rect.Height - d, d, d, 90.0f, 90.0f);
    path->CloseFigure();
    return path;
}

const float kCos[10] = {0.000000f, 0.587785f, 0.951057f, 0.951057f, 0.587785f,
                        0.000000f, -0.587785f, -0.951057f, -0.951057f, -0.587785f};
const float kSin[10] = {-1.000000f, -0.809017f, -0.309017f, 0.309017f, 0.809017f,
                        1.000000f, 0.809017f, 0.309017f, -0.309017f, -0.809017f};

void star_outline(const RECT& rect, Gdiplus::PointF points[11]) {
    float cx = (rect.left + rect.right) / 2.0f;
    float cy = (rect.top + rect.bottom) / 2.0f;
    float side = std::min(static_cast<float>(rect.right - rect.left),
                          static_cast<float>(rect.bottom - rect.top));
    float outer = side / 2.0f - std::max(1.5f, side / 11.0f);
    float inner = outer * 0.45f;
    for (int i = 0; i < 10; ++i) {
        float radius = (i % 2 == 0) ? outer : inner;
        points[i].X = cx + radius * kCos[i];
        points[i].Y = cy + radius * kSin[i];
    }
    points[10] = points[0];
}

void star_outline(const RECT& rect, Gdiplus::Point points[11]) {
    Gdiplus::PointF floating[11];
    star_outline(rect, floating);
    for (int i = 0; i < 11; ++i) {
        points[i].X = static_cast<INT>(floating[i].X + 0.5f);
        points[i].Y = static_cast<INT>(floating[i].Y + 0.5f);
    }
}

int encoder_png_clsid(CLSID* clsid) {
    UINT count = 0;
    UINT bytes = 0;
    Gdiplus::GetImageEncodersSize(&count, &bytes);
    if (count == 0) return -1;
    std::vector<Gdiplus::ImageCodecInfo> codecs(count);
    if (Gdiplus::GetImageEncoders(count, sizeof(Gdiplus::ImageCodecInfo),
                                  codecs.data()) != Gdiplus::Ok) {
        return -1;
    }
    for (UINT i = 0; i < count; ++i) {
        if (wcscmp(codecs[i].MimeType, L"image/png") == 0) {
            *clsid = codecs[i].Clsid;
            return 0;
        }
    }
    return -1;
}

IStream* stream_from_bytes(const unsigned char* data, std::size_t size) {
    if (size == 0 || size > (8u << 20)) return nullptr;
    HGLOBAL mem = GlobalAlloc(GMEM_MOVEABLE, size);
    if (!mem) return nullptr;
    void* dest = GlobalLock(mem);
    if (!dest) {
        GlobalFree(mem);
        return nullptr;
    }
    std::memcpy(dest, data, size);
    GlobalUnlock(mem);
    IStream* stream = nullptr;
    if (CreateStreamOnHGlobal(mem, TRUE, &stream) != S_OK) {
        GlobalFree(mem);
        return nullptr;
    }
    return stream;
}

Gdiplus::Bitmap* decode_stream(IStream* stream) {
    Gdiplus::Bitmap* raw = Gdiplus::Bitmap::FromStream(stream);
    if (!raw) return nullptr;
    Gdiplus::Bitmap* owned = raw->Clone(0, 0, raw->GetWidth(), raw->GetHeight(),
                                        PixelFormat32bppARGB);
    delete raw;
    return owned;
}

bool is_png(const unsigned char* d, std::size_t n) {
    return n >= 8 && d[0] == 0x89 && d[1] == 0x50 && d[2] == 0x4E && d[3] == 0x47;
}

bool is_jpeg(const unsigned char* d, std::size_t n) {
    return n >= 3 && d[0] == 0xFF && d[1] == 0xD8 && d[2] == 0xFF;
}

bool is_bmp(const unsigned char* d, std::size_t n) {
    return n >= 2 && d[0] == 'B' && d[1] == 'M';
}

bool is_ico(const unsigned char* d, std::size_t n) {
    return n >= 4 && d[0] == 0 && d[1] == 0 && d[2] == 1 && d[3] == 0;
}

Gdiplus::Bitmap* decode_any(const unsigned char* data, std::size_t size) {
    IStream* stream = stream_from_bytes(data, size);
    if (!stream) return nullptr;
    Gdiplus::Bitmap* bitmap = decode_stream(stream);
    stream->Release();
    return bitmap;
}

// ICO: hand-pick the largest entry (most favicons are PNG-in-ICO at 16/32/48).
Gdiplus::Bitmap* decode_ico(const unsigned char* data, std::size_t size) {
    if (size < 6) return nullptr;
    UINT count = data[4] | (static_cast<UINT>(data[5]) << 8);
    size_t best_offset = 0;
    size_t best_bytes = 0;
    int best_w = -1;
    for (UINT i = 0; i < count && i < 64; ++i) {
        size_t at = 6 + static_cast<size_t>(i) * 16;
        if (at + 16 > size) break;
        int w = data[at] == 0 ? 256 : data[at];
        UINT bytes = static_cast<UINT>(data[at + 8]) | (static_cast<UINT>(data[at + 9]) << 8)
            | (static_cast<UINT>(data[at + 10]) << 16)
            | (static_cast<UINT>(data[at + 11]) << 24);
        UINT offset = static_cast<UINT>(data[at + 12]) | (static_cast<UINT>(data[at + 13]) << 8)
            | (static_cast<UINT>(data[at + 14]) << 16)
            | (static_cast<UINT>(data[at + 15]) << 24);
        if (w > best_w && bytes > 0 && offset + bytes <= size) {
            best_w = w;
            best_offset = offset;
            best_bytes = bytes;
        }
    }
    if (best_w < 0) return decode_any(data, size);
    const unsigned char* payload = data + best_offset;
    if (best_bytes >= 8 && is_png(payload, best_bytes)) {
        Gdiplus::Bitmap* bitmap = decode_any(payload, best_bytes);
        if (bitmap) return bitmap;
    }
    return decode_any(data, size);
}

}  // namespace

void init() {
    if (g_token) return;
    Gdiplus::GdiplusStartupInput input;
    Gdiplus::GdiplusStartup(&g_token, &input, nullptr);
}

const Palette& palette() {
    static bool seeded = false;
    if (!seeded) {
        apply_named_theme("auto", std::string());
        seeded = true;
    }
    return dark_chrome() ? g_pal_dark : g_pal_light;
}

bool dark_chrome() {
    return g_dark_chrome;
}

void set_dark_chrome(bool dark) {
    g_dark_chrome = dark;
}

bool system_prefers_dark() {
    // HKCU\...\Themes\Personalize\AppsUseLightTheme: 0 = dark app mode.
    DWORD value = 1;
    DWORD size = sizeof(value);
    LSTATUS status = RegGetValueW(HKEY_CURRENT_USER,
        L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
        L"AppsUseLightTheme", RRF_RT_REG_DWORD, nullptr, &value, &size);
    return status == ERROR_SUCCESS && value == 0;
}

const std::vector<std::string>& theme_names() {
    static const std::vector<std::string> names = {
        "auto", "light", "dark", "nord", "dracula", "catppuccin", "solarized",
        "sakura", "crimson"
    };
    return names;
}

bool is_known_theme(const std::string& name) {
    return name == "auto" || name == "light" || name == "dark"
        || find_theme(name) != nullptr;
}

void apply_named_theme(const std::string& name, const std::string& accent_hex) {
    g_pal_light = builtin_light_palette();
    g_pal_dark = builtin_dark_palette();
    g_aux_light = builtin_light_aux();
    g_aux_dark = builtin_dark_aux();

    if (const ThemeSpec* spec = find_theme(name)) {
        // A named palette fills both slots: the theme is the character of the
        // browser now, not a light/dark flip.
        g_pal_light = spec->palette;
        g_pal_dark = spec->palette;
        g_aux_light = spec->aux;
        g_aux_dark = spec->aux;
        g_dark_chrome = spec->dark;
    } else if (name == "light") {
        g_dark_chrome = false;
    } else if (name == "dark") {
        g_dark_chrome = true;
    } else {
        // "auto" (and anything unknown): follow the Windows app mode.
        g_dark_chrome = system_prefers_dark();
    }

    if (COLORREF accent = parse_hex_color(accent_hex); accent != CLR_INVALID) {
        g_pal_light.accent = accent;
        g_pal_light.omnibox_focus = accent;
        g_pal_dark.accent = accent;
        g_pal_dark.omnibox_focus = accent;
    }

    refresh_aux_brushes();
}

bool theme_preview(const std::string& name, COLORREF* bg, COLORREF* text,
                   COLORREF* accent) {
    if (!bg || !text || !accent) return false;
    if (name == "auto" || name == "light" || name == "dark") {
        bool dark = name == "dark"
            || (name == "auto" && system_prefers_dark());
        const Palette& p = dark ? builtin_dark_palette() : builtin_light_palette();
        *bg = p.chrome_bg;
        *text = p.chrome_text;
        *accent = p.accent;
        return true;
    }
    if (const ThemeSpec* spec = find_theme(name)) {
        *bg = spec->palette.chrome_bg;
        *text = spec->palette.chrome_text;
        *accent = spec->palette.accent;
        return true;
    }
    return false;
}

const AuxTheme& aux_theme() {
    static bool seeded = false;
    if (!seeded) {
        apply_named_theme("auto", std::string());
        seeded = true;
    }
    return dark_chrome() ? g_aux_dark : g_aux_light;
}

HBRUSH aux_surface_brush() {
    static bool seeded = false;
    if (!seeded) {
        refresh_aux_brushes();
        seeded = true;
    }
    return dark_chrome() ? g_surface_brush_dark : g_surface_brush_light;
}

HBRUSH aux_field_brush() {
    static bool seeded = false;
    if (!seeded) {
        refresh_aux_brushes();
        seeded = true;
    }
    return dark_chrome() ? g_field_brush_dark : g_field_brush_light;
}

COLORREF blend(COLORREF base, COLORREF color, int alpha) {
    if (alpha < 0) alpha = 0;
    if (alpha > 255) alpha = 255;
    auto mix = [alpha](int b, int c) {
        return static_cast<int>((b * (255 - alpha) + c * alpha) / 255.0 + 0.5);
    };
    return RGB(mix(GetRValue(base), GetRValue(color)),
               mix(GetGValue(base), GetGValue(color)),
               mix(GetBValue(base), GetBValue(color)));
}

void fill_round_rect(HDC dc, const RECT& rect, int radius_dp, int dpi,
                     COLORREF color) {
    if (rect.right <= rect.left || rect.bottom <= rect.top) return;
    Gdiplus::Graphics* g = graphics_on(dc);
    float radius = static_cast<float>(MulDiv(radius_dp, dpi, 96));
    Gdiplus::RectF area(static_cast<Gdiplus::REAL>(rect.left),
                        static_cast<Gdiplus::REAL>(rect.top),
                        static_cast<Gdiplus::REAL>(rect.right - rect.left),
                        static_cast<Gdiplus::REAL>(rect.bottom - rect.top));
    radius = std::max(0.5f, std::min(radius, std::min(area.Width, area.Height)
                                               / 2.0f));
    Gdiplus::GraphicsPath* path = round_path(area, radius);
    Gdiplus::SolidBrush brush(make_color(color));
    g->FillPath(&brush, path);
    delete path;
    delete g;
}

void stroke_round_rect(HDC dc, const RECT& rect, int radius_dp, int dpi,
                       COLORREF color, float width_dp) {
    if (rect.right <= rect.left || rect.bottom <= rect.top) return;
    Gdiplus::Graphics* g = graphics_on(dc);
    float radius = static_cast<float>(MulDiv(radius_dp, dpi, 96));
    float width = std::max(0.75f, static_cast<float>(MulDiv(
        static_cast<int>(width_dp * 4.0f + 0.5f), dpi, 96)) / 4.0f);
    Gdiplus::RectF area(static_cast<Gdiplus::REAL>(rect.left),
                        static_cast<Gdiplus::REAL>(rect.top),
                        static_cast<Gdiplus::REAL>(rect.right - rect.left - 1),
                        static_cast<Gdiplus::REAL>(rect.bottom - rect.top - 1));
    radius = std::max(0.5f, std::min(radius, std::min(area.Width, area.Height)
                                               / 2.0f));
    Gdiplus::GraphicsPath* path = round_path(area, radius);
    auto pen = make_pen(color, width);
    g->DrawPath(pen.get(), path);
    delete path;
    delete g;
}

void draw_glyph(HDC dc, const RECT& rect, Glyph glyph, COLORREF color,
                float stroke_dp, int dpi, int spin_phase) {
    if (rect.right <= rect.left || rect.bottom <= rect.top) return;
    Gdiplus::Graphics* g = graphics_on(dc);
    float stroke = std::max(1.0f, static_cast<float>(MulDiv(
        static_cast<int>(stroke_dp * 4.0f + 0.5f), dpi, 96)) / 4.0f);
    auto pen = make_pen(color, stroke);

    float side = static_cast<float>(std::min(rect.right - rect.left,
                                             rect.bottom - rect.top));
    float unit = side / 16.0f;
    float cx = (rect.left + rect.right) / 2.0f;
    float cy = (rect.top + rect.bottom) / 2.0f;
    auto X = [cx, unit](float gx) { return cx + (gx - 8.0f) * unit; };
    auto Y = [cy, unit](float gy) { return cy + (gy - 8.0f) * unit; };

    switch (glyph) {
        case Glyph::Back: {
            g->DrawLine(pen.get(), X(12), Y(8), X(4.5f), Y(8));
            g->DrawLine(pen.get(), X(8.5f), Y(4), X(4.5f), Y(8));
            g->DrawLine(pen.get(), X(4.5f), Y(8), X(8.5f), Y(12));
            break;
        }
        case Glyph::Forward: {
            g->DrawLine(pen.get(), X(4), Y(8), X(11.5f), Y(8));
            g->DrawLine(pen.get(), X(7.5f), Y(4), X(11.5f), Y(8));
            g->DrawLine(pen.get(), X(11.5f), Y(8), X(7.5f), Y(12));
            break;
        }
        case Glyph::Reload: {
            float r = 4.6f;
            Gdiplus::RectF box(X(8 - r), Y(8 - r), 2 * r * unit, 2 * r * unit);
            float start = -55.0f;
            g->DrawArc(pen.get(), box, start, 290.0f);
            float angle = start * kPi / 180.0f;
            float hx = X(8 + r * std::cos(angle));
            float hy = Y(8 + r * std::sin(angle));
            float t = angle + kPi / 2.0f;
            float head = 3.4f;
            g->DrawLine(pen.get(), hx, hy, hx + std::cos(t + 0.5f) * head * unit,
                        hy + std::sin(t + 0.5f) * head * unit);
            g->DrawLine(pen.get(), hx, hy, hx + std::cos(t - 0.5f) * head * unit,
                        hy + std::sin(t - 0.5f) * head * unit);
            break;
        }
        case Glyph::Plus: {
            g->DrawLine(pen.get(), X(8), Y(3.5f), X(8), Y(12.5f));
            g->DrawLine(pen.get(), X(3.5f), Y(8), X(12.5f), Y(8));
            break;
        }
        case Glyph::Close:
        case Glyph::CaptionClose: {
            float inset = glyph == Glyph::CaptionClose ? 5.2f : 4.5f;
            g->DrawLine(pen.get(), X(inset), Y(inset), X(16 - inset),
                        Y(16 - inset));
            g->DrawLine(pen.get(), X(16 - inset), Y(inset), X(inset),
                        Y(16 - inset));
            break;
        }
        case Glyph::Star: {
            Gdiplus::PointF star[11];
            star_outline(rect, star);
            g->DrawLines(pen.get(), star, 10);
            break;
        }
        case Glyph::StarFilled: {
            Gdiplus::Point star[11];
            star_outline(rect, star);
            Gdiplus::SolidBrush brush(make_color(color));
            g->FillPolygon(&brush, star, 10);
            break;
        }
        case Glyph::Gear: {
            float r = 3.1f;
            Gdiplus::RectF box(X(8 - r), Y(8 - r), 2 * r * unit, 2 * r * unit);
            g->DrawEllipse(pen.get(), box);
            for (int i = 0; i < 8; ++i) {
                float a = i * kPi / 4.0f;
                float c = std::cos(a);
                float s = std::sin(a);
                g->DrawLine(pen.get(), X(8 + 4.3f * c), Y(8 + 4.3f * s),
                            X(8 + 6.2f * c), Y(8 + 6.2f * s));
            }
            Gdiplus::SolidBrush brush(make_color(color));
            g->FillEllipse(&brush, X(8 - 1.2f), Y(8 - 1.2f), 2.4f * unit,
                           2.4f * unit);
            break;
        }
        case Glyph::Search: {
            float r = 4.0f;
            Gdiplus::RectF box(X(7 - r), Y(7 - r), 2 * r * unit, 2 * r * unit);
            g->DrawEllipse(pen.get(), box);
            g->DrawLine(pen.get(), X(10.1f), Y(10.1f), X(13.4f), Y(13.4f));
            break;
        }
        case Glyph::Minimize: {
            g->DrawLine(pen.get(), X(5), Y(8.5f), X(11), Y(8.5f));
            break;
        }
        case Glyph::Maximize: {
            Gdiplus::RectF box(X(4.5f), Y(4.5f), 7 * unit, 7 * unit);
            g->DrawRectangle(pen.get(), box);
            break;
        }
        case Glyph::Restore: {
            g->DrawLine(pen.get(), X(5.5f), Y(7.2f), X(5.5f), Y(3.5f));
            g->DrawLine(pen.get(), X(5.5f), Y(3.5f), X(12.5f), Y(3.5f));
            g->DrawLine(pen.get(), X(12.5f), Y(3.5f), X(12.5f), Y(7.2f));
            Gdiplus::RectF front(X(3.5f), Y(5.5f), 7 * unit, 7 * unit);
            g->DrawRectangle(pen.get(), front);
            break;
        }
        case Glyph::Incognito: {
            g->DrawLine(pen.get(), X(2.6f), Y(7.4f), X(13.4f), Y(7.4f));
            Gdiplus::RectF dome(X(5.0f), Y(3.6f), 6.0f * unit, 6.0f * unit);
            g->DrawArc(pen.get(), dome, 180.0f, 180.0f);
            float r = 2.1f;
            g->DrawEllipse(pen.get(), Gdiplus::RectF(X(5.4f - r), Y(10.8f - r),
                2 * r * unit, 2 * r * unit));
            g->DrawEllipse(pen.get(), Gdiplus::RectF(X(10.6f - r), Y(10.8f - r),
                2 * r * unit, 2 * r * unit));
            g->DrawLine(pen.get(), X(7.5f), Y(10.8f), X(8.5f), Y(10.8f));
            break;
        }
        case Glyph::Globe: {
            float r = 5.4f;
            Gdiplus::RectF box(X(8 - r), Y(8 - r), 2 * r * unit, 2 * r * unit);
            g->DrawEllipse(pen.get(), box);
            g->DrawEllipse(pen.get(), Gdiplus::RectF(X(8 - 2.4f), Y(8 - r),
                4.8f * unit, 2 * r * unit));
            g->DrawLine(pen.get(), X(2.6f), Y(8), X(13.4f), Y(8));
            break;
        }
        case Glyph::Spinner: {
            float r = 5.2f;
            Gdiplus::RectF box(X(8 - r), Y(8 - r), 2 * r * unit, 2 * r * unit);
            float start = static_cast<float>(spin_phase % 16) * 22.5f;
            g->DrawArc(pen.get(), box, start, 100.0f);
            break;
        }
        case Glyph::Palette: {
            // Droplet: apex at the top, round belly at the bottom.
            float r = 3.3f;
            Gdiplus::RectF box(X(8 - r), Y(9.7f - r), 2 * r * unit, 2 * r * unit);
            auto at = [&](float deg) {
                float a = deg * kPi / 180.0f;
                return Gdiplus::PointF(X(8 + r * std::cos(a)),
                                       Y(9.7f + r * std::sin(a)));
            };
            Gdiplus::GraphicsPath drop;
            drop.StartFigure();
            drop.AddLine(X(8), Y(2.6f), at(245).X, at(245).Y);
            drop.AddArc(box, 245.0f, 310.0f);
            drop.CloseFigure();
            g->DrawPath(pen.get(), &drop);
            break;
        }
        case Glyph::Shield: {
            Gdiplus::GraphicsPath shield;
            shield.StartFigure();
            shield.AddLine(X(4), Y(3.6f), X(12), Y(3.6f));
            shield.AddLine(X(12), Y(3.6f), X(12), Y(8.0f));
            shield.AddBezier(X(12), Y(8.0f), X(12), Y(10.8f), X(10.2f), Y(12.4f),
                             X(8), Y(13.4f));
            shield.AddBezier(X(8), Y(13.4f), X(5.8f), Y(12.4f), X(4), Y(10.8f),
                             X(4), Y(8.0f));
            shield.CloseFigure();
            g->DrawPath(pen.get(), &shield);
            break;
        }
        case Glyph::Database: {
            Gdiplus::RectF top(X(3), Y(3.2f), 10 * unit, 3.2f * unit);
            Gdiplus::RectF mid(X(3), Y(6.4f), 10 * unit, 3.2f * unit);
            Gdiplus::RectF bot(X(3), Y(9.6f), 10 * unit, 3.2f * unit);
            g->DrawEllipse(pen.get(), top);
            g->DrawLine(pen.get(), X(3), Y(4.8f), X(3), Y(11.2f));
            g->DrawLine(pen.get(), X(13), Y(4.8f), X(13), Y(11.2f));
            g->DrawArc(pen.get(), mid, 0.0f, 180.0f);
            g->DrawArc(pen.get(), bot, 0.0f, 180.0f);
            break;
        }
        case Glyph::Home: {
            g->DrawLine(pen.get(), X(2.8f), Y(8.2f), X(8), Y(3.4f));
            g->DrawLine(pen.get(), X(8), Y(3.4f), X(13.2f), Y(8.2f));
            g->DrawLine(pen.get(), X(4.4f), Y(7.4f), X(4.4f), Y(12.6f));
            g->DrawLine(pen.get(), X(4.4f), Y(12.6f), X(11.6f), Y(12.6f));
            g->DrawLine(pen.get(), X(11.6f), Y(12.6f), X(11.6f), Y(7.4f));
            g->DrawLine(pen.get(), X(6.8f), Y(12.6f), X(6.8f), Y(9.8f));
            g->DrawLine(pen.get(), X(6.8f), Y(9.8f), X(9.2f), Y(9.8f));
            g->DrawLine(pen.get(), X(9.2f), Y(9.8f), X(9.2f), Y(12.6f));
            break;
        }
        case Glyph::Briefcase: {
            g->DrawLine(pen.get(), X(3), Y(6), X(13), Y(6));
            g->DrawLine(pen.get(), X(13), Y(6), X(13), Y(12.4f));
            g->DrawLine(pen.get(), X(13), Y(12.4f), X(3), Y(12.4f));
            g->DrawLine(pen.get(), X(3), Y(12.4f), X(3), Y(6));
            g->DrawLine(pen.get(), X(6), Y(6), X(6), Y(4.4f));
            g->DrawLine(pen.get(), X(6), Y(4.4f), X(10), Y(4.4f));
            g->DrawLine(pen.get(), X(10), Y(4.4f), X(10), Y(6));
            g->DrawLine(pen.get(), X(3), Y(9), X(13), Y(9));
            break;
        }
        case Glyph::Code: {
            g->DrawLine(pen.get(), X(5.8f), Y(5.6f), X(3.2f), Y(8));
            g->DrawLine(pen.get(), X(3.2f), Y(8), X(5.8f), Y(10.4f));
            g->DrawLine(pen.get(), X(10.2f), Y(5.6f), X(12.8f), Y(8));
            g->DrawLine(pen.get(), X(12.8f), Y(8), X(10.2f), Y(10.4f));
            g->DrawLine(pen.get(), X(9), Y(4.6f), X(7), Y(11.4f));
            break;
        }
        case Glyph::Gamepad: {
            float rx = 5.6f;
            float ry = 4.0f;
            g->DrawEllipse(pen.get(), Gdiplus::RectF(X(8 - rx), Y(8 - ry),
                2 * rx * unit, 2 * ry * unit));
            g->DrawLine(pen.get(), X(5.4f), Y(8), X(7.4f), Y(8));
            g->DrawLine(pen.get(), X(6.4f), Y(7), X(6.4f), Y(9));
            float dot = 0.7f;
            g->DrawEllipse(pen.get(), Gdiplus::RectF(X(10 - dot), Y(6.8 - dot),
                2 * dot * unit, 2 * dot * unit));
            g->DrawEllipse(pen.get(), Gdiplus::RectF(X(11.4f - dot), Y(9 - dot),
                2 * dot * unit, 2 * dot * unit));
            break;
        }
        case Glyph::Cart: {
            g->DrawLine(pen.get(), X(2.8f), Y(4.4f), X(4.4f), Y(4.4f));
            g->DrawLine(pen.get(), X(4.4f), Y(4.4f), X(5.8f), Y(10.6f));
            g->DrawLine(pen.get(), X(5.8f), Y(10.6f), X(12.2f), Y(10.6f));
            g->DrawLine(pen.get(), X(12.2f), Y(10.6f), X(13.4f), Y(6.4f));
            g->DrawLine(pen.get(), X(13.4f), Y(6.4f), X(5.2f), Y(6.4f));
            float r = 1.0f;
            g->DrawEllipse(pen.get(), Gdiplus::RectF(X(6.8f - r), Y(12.8f - r),
                2 * r * unit, 2 * r * unit));
            g->DrawEllipse(pen.get(), Gdiplus::RectF(X(11 - r), Y(12.8f - r),
                2 * r * unit, 2 * r * unit));
            break;
        }
        case Glyph::News: {
            g->DrawLine(pen.get(), X(3.4f), Y(3.6f), X(12.6f), Y(3.6f));
            g->DrawLine(pen.get(), X(12.6f), Y(3.6f), X(12.6f), Y(12.4f));
            g->DrawLine(pen.get(), X(12.6f), Y(12.4f), X(3.4f), Y(12.4f));
            g->DrawLine(pen.get(), X(3.4f), Y(12.4f), X(3.4f), Y(3.6f));
            g->DrawLine(pen.get(), X(5.4f), Y(6.4f), X(10.6f), Y(6.4f));
            g->DrawLine(pen.get(), X(5.4f), Y(8.6f), X(10.6f), Y(8.6f));
            g->DrawLine(pen.get(), X(5.4f), Y(10.4f), X(8.8f), Y(10.4f));
            break;
        }
        case Glyph::Music: {
            float r = 1.5f;
            g->DrawEllipse(pen.get(), Gdiplus::RectF(X(6.2f - r), Y(11 - r),
                2 * r * unit, 2 * r * unit));
            g->DrawLine(pen.get(), X(7.7f), Y(11), X(7.7f), Y(4));
            g->DrawLine(pen.get(), X(7.7f), Y(4), X(11), Y(5.2f));
            g->DrawLine(pen.get(), X(11), Y(5.2f), X(11), Y(7));
            break;
        }
        case Glyph::Mail: {
            g->DrawLine(pen.get(), X(3), Y(4.6f), X(13), Y(4.6f));
            g->DrawLine(pen.get(), X(13), Y(4.6f), X(13), Y(11.4f));
            g->DrawLine(pen.get(), X(13), Y(11.4f), X(3), Y(11.4f));
            g->DrawLine(pen.get(), X(3), Y(11.4f), X(3), Y(4.6f));
            g->DrawLine(pen.get(), X(3.4f), Y(5.2f), X(8), Y(8.6f));
            g->DrawLine(pen.get(), X(8), Y(8.6f), X(12.6f), Y(5.2f));
            break;
        }
        case Glyph::Heart: {
            Gdiplus::GraphicsPath heart;
            heart.StartFigure();
            heart.AddBezier(X(8), Y(12.8f), X(4.4f), Y(10.2f), X(2.8f), Y(8.6f),
                            X(2.8f), Y(6.6f));
            heart.AddBezier(X(2.8f), Y(6.6f), X(2.8f), Y(4.4f), X(5.8f), Y(4),
                            X(8), Y(6.6f));
            heart.AddBezier(X(8), Y(6.6f), X(10.2f), Y(4), X(13.2f), Y(4.4f),
                            X(13.2f), Y(6.6f));
            heart.AddBezier(X(13.2f), Y(6.6f), X(13.2f), Y(8.6f), X(11.6f),
                            Y(10.2f), X(8), Y(12.8f));
            heart.CloseFigure();
            g->DrawPath(pen.get(), &heart);
            break;
        }
        case Glyph::Flame: {
            Gdiplus::GraphicsPath flame;
            flame.StartFigure();
            flame.AddBezier(X(8), Y(2.6f), X(9.6f), Y(5), X(12.4f), Y(6.8f),
                            X(12.4f), Y(9.6f));
            flame.AddBezier(X(12.4f), Y(9.6f), X(12.4f), Y(11.8f), X(10.4f),
                            Y(13.4f), X(8), Y(13.4f));
            flame.AddBezier(X(8), Y(13.4f), X(5.6f), Y(13.4f), X(3.6f), Y(11.8f),
                            X(3.6f), Y(9.6f));
            flame.AddBezier(X(3.6f), Y(9.6f), X(3.6f), Y(7.4f), X(5), Y(6.2f),
                            X(6.2f), Y(4.6f));
            flame.AddBezier(X(6.2f), Y(4.6f), X(6.9f), Y(3.7f), X(7.6f),
                            Y(3.2f), X(8), Y(2.6f));
            flame.CloseFigure();
            g->DrawPath(pen.get(), &flame);
            break;
        }
        default:
            break;
    }

    delete g;
}

bool glyph_from_name(const std::string& name, Glyph* out) {
    static const struct {
        const char* name;
        Glyph glyph;
    } kMap[] = {
        {"home", Glyph::Home},       {"briefcase", Glyph::Briefcase},
        {"code", Glyph::Code},       {"gamepad", Glyph::Gamepad},
        {"cart", Glyph::Cart},       {"news", Glyph::News},
        {"music", Glyph::Music},     {"mail", Glyph::Mail},
        {"star", Glyph::Star},       {"heart", Glyph::Heart},
        {"flame", Glyph::Flame},     {"globe", Glyph::Globe},
    };
    for (const auto& entry : kMap) {
        if (name == entry.name) {
            if (out) *out = entry.glyph;
            return true;
        }
    }
    return false;
}

void draw_image_fit(HDC dc, const RECT& rect, void* image) {
    if (!image || rect.right <= rect.left || rect.bottom <= rect.top) return;
    Gdiplus::Graphics* g = graphics_on(dc);
    Gdiplus::RectF area(static_cast<Gdiplus::REAL>(rect.left),
                        static_cast<Gdiplus::REAL>(rect.top),
                        static_cast<Gdiplus::REAL>(rect.right - rect.left),
                        static_cast<Gdiplus::REAL>(rect.bottom - rect.top));
    g->DrawImage(static_cast<Gdiplus::Bitmap*>(image), area);
    delete g;
}

void* decode_image_bytes(const unsigned char* data, std::size_t size) {
    init();
    if (!data || size < 4) return nullptr;
    Gdiplus::Bitmap* bitmap = nullptr;
    if (is_ico(data, size)) bitmap = decode_ico(data, size);
    if (!bitmap && (is_png(data, size) || is_jpeg(data, size) || is_bmp(data, size)
                    || is_ico(data, size))) {
        bitmap = decode_any(data, size);
    }
    if (!bitmap) return nullptr;
    if (bitmap->GetWidth() == 0 || bitmap->GetHeight() == 0) {
        delete bitmap;
        return nullptr;
    }
    return bitmap;
}

void* load_image_file(const std::string& utf8_path) {
    std::FILE* file = std::fopen(utf8_path.c_str(), "rb");
    if (!file) return nullptr;
    std::vector<unsigned char> bytes;
    unsigned char buffer[8192];
    std::size_t read = 0;
    while ((read = std::fread(buffer, 1, sizeof(buffer), file)) > 0) {
        bytes.insert(bytes.end(), buffer, buffer + read);
    }
    std::fclose(file);
    return decode_image_bytes(bytes.data(), bytes.size());
}

void* resize_image(void* image, int px) {
    if (!image || px <= 0) return nullptr;
    auto* src = static_cast<Gdiplus::Bitmap*>(image);
    auto* out = new Gdiplus::Bitmap(px, px, PixelFormat32bppARGB);
    Gdiplus::Graphics* g = new Gdiplus::Graphics(out);
    g->SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
    g->SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
    g->DrawImage(src, Gdiplus::RectF(0, 0, static_cast<Gdiplus::REAL>(px),
                                     static_cast<Gdiplus::REAL>(px)));
    delete g;
    return out;
}

bool save_image_png(void* image, const std::string& utf8_path) {
    if (!image) return false;
    CLSID clsid;
    if (encoder_png_clsid(&clsid) != 0) return false;
    int wide = MultiByteToWideChar(CP_UTF8, 0, utf8_path.c_str(),
                                   static_cast<int>(utf8_path.size()), nullptr, 0);
    std::wstring path(static_cast<size_t>(wide), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, utf8_path.c_str(),
                        static_cast<int>(utf8_path.size()), path.data(), wide);
    auto* bitmap = static_cast<Gdiplus::Bitmap*>(image);
    return bitmap->Save(path.c_str(), &clsid, nullptr) == Gdiplus::Ok;
}

}
}
