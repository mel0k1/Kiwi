#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <cstddef>
#include <string>

namespace kiwi {
namespace gfx {

// One-time GDI+ startup (never shut down; icons and favicons outlive callers).
void init();

// Neutral light chrome palette shared by every painted surface.
struct Palette {
    COLORREF chrome_bg;
    COLORREF chrome_text;
    COLORREF chrome_muted;
    COLORREF active_tab_bg;
    COLORREF hover_chip;
    COLORREF pressed_chip;
    COLORREF outline;
    COLORREF omnibox_bg;
    COLORREF omnibox_border;
    COLORREF omnibox_focus;
    COLORREF accent;
    COLORREF separator;
    COLORREF content_bg;
    COLORREF danger_hover;
};

const Palette& palette();

enum class Glyph {
    Back,
    Forward,
    Reload,
    Plus,
    Close,
    Star,
    StarFilled,
    Gear,
    Search,
    Minimize,
    Maximize,
    Restore,
    CaptionClose,
    Incognito,
    Globe,
    Spinner
};

// Anti-aliased vector glyph centered in |rect| on a 16x16 design grid.
// |spin_phase| (0..15) rotates the Spinner arc.
void draw_glyph(HDC dc, const RECT& rect, Glyph glyph, COLORREF color,
                float stroke_dp, int dpi, int spin_phase = 0);

void fill_round_rect(HDC dc, const RECT& rect, int radius_dp, int dpi,
                     COLORREF color);
void stroke_round_rect(HDC dc, const RECT& rect, int radius_dp, int dpi,
                       COLORREF color, float width_dp);

// Draws an opaque image handle (see decode_image_bytes) fitted into |rect|.
void draw_image_fit(HDC dc, const RECT& rect, void* image);

// Opaque Gdiplus::Bitmap* handles; the caller never frees them (cached for
// the process lifetime). decode handles PNG / JPEG / BMP / ICO bytes.
void* decode_image_bytes(const unsigned char* data, std::size_t size);
void* load_image_file(const std::string& utf8_path);
void* resize_image(void* image, int px);
bool save_image_png(void* image, const std::string& utf8_path);

// Pre-blends |alpha| (0..255) of |color| over |base| — keeps every painted
// pixel opaque so the same code path works on the Mica backdrop.
COLORREF blend(COLORREF base, COLORREF color, int alpha);

}
}
