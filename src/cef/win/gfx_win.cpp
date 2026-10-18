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
    static Palette p = []() {
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
    }();
    return p;
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
        default:
            break;
    }

    delete g;
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
