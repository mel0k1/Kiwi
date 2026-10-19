// Kiwi settings — Firefox-style sectioned window: a navigation sidebar
// (General / Appearance / Search / Privacy / Data), one page of controls per
// section and instant apply (no Save button; edits commit on focus-out and
// on close). Appearance carries the customization surface: shell style, rail
// side, chrome backdrop, the ready-made theme chips and the custom accent.

#include "settings_win.hpp"

#include "../cef_runtime.hpp"
#include "aux_theme_win.hpp"
#include "cef_shell_win.hpp"
#include "clear_data_win.hpp"
#include "search_engines_win.hpp"
#include "../../style_store.hpp"
#include "history_win.hpp"
#include "site_rules_win.hpp"
#include "updater_win.hpp"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <windowsx.h>
#include <commdlg.h>

#include <algorithm>
#include <functional>
#include <string>
#include <vector>

namespace kiwi {

namespace {

constexpr wchar_t kClass[] = L"KiwiSettingsWindow";

enum {
    ID_STYLE = 3001,
    ID_RAILSIDE,
    ID_BACKDROP,
    ID_ACCENT,
    ID_ACCENT_RESET,
    ID_STYLE_LABEL,
    ID_RAILSIDE_LABEL,
    ID_BACKDROP_LABEL,
    ID_HOMEPAGE,
    ID_HOMEPAGE_LABEL,
    ID_SEARCH,
    ID_SEARCH_LABEL,
    ID_DOH,
    ID_DOH_LABEL,
    ID_COOKIES,
    ID_COOKIES_LABEL,
    ID_JAVASCRIPT,
    ID_AUTOPLAY,
    ID_GPC,
    ID_AUDITING,
    ID_FP_WEBGL,
    ID_FP_CANVAS,
    ID_PRIVATE,
    ID_START_PRIVATE,
    ID_SESSION,
    ID_HISTORY_SAVE,
    ID_ENGINES,
    ID_SITERULES,
    ID_HISTORY,
    ID_CLEARDATA,
    ID_UPDATE,
    ID_CLOSEWIN,
    // Chip ids live above every named id: kThemeChipCount consecutive ids
    // are reserved so the range checks stay collision-free.
    ID_THEME_BASE,
};

enum {
    SECTION_GENERAL = 0,
    SECTION_APPEARANCE,
    SECTION_SEARCH,
    SECTION_PRIVACY,
    SECTION_DATA,
    SECTION_COUNT
};

struct SectionDef {
    const wchar_t* nav;     // sidebar caption
    const wchar_t* heading; // page heading
    gfx::Glyph glyph;
};

constexpr SectionDef kSections[SECTION_COUNT] = {
    {L"General", L"General", gfx::Glyph::Home},
    {L"Appearance", L"Appearance", gfx::Glyph::Palette},
    {L"Search", L"Search", gfx::Glyph::Search},
    {L"Privacy", L"Privacy and security", gfx::Glyph::Shield},
    {L"Data", L"Data and updates", gfx::Glyph::Database},
};

constexpr int kThemeChipCount = 9;  // gfx::theme_names().size()
constexpr UINT_PTR kFlashTimer = 1;

// Window message that re-reads the persisted rail side into the dialog: the
// zen-rail drag gesture in the shell writes style.toml directly and then
// pokes every open settings window through sync_settings_rail_side(), so
// the "Panel side" combo never goes stale while the dialog is up.
constexpr UINT kSyncRailSideMsg = WM_APP + 0x51;

struct SettingsState {
    HWND hwnd = nullptr;
    std::function<void()> on_style_changed;
    std::string original_style;
    std::string original_backdrop;
    std::string original_theme;
    std::string original_accent;
    std::string original_rail_side;
    std::string theme;    // pending theme from the chips
    std::string accent;   // pending accent override ("" = theme default)
    int section = SECTION_GENERAL;
    int hover_section = -1;
    bool saved_flash = false;
    HFONT font = nullptr;
    HFONT bold = nullptr;
    HFONT title = nullptr;
    HFONT small = nullptr;
    std::vector<HWND> pages[SECTION_COUNT];
};

SettingsState* state_for(HWND hwnd) {
    return reinterpret_cast<SettingsState*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
}

int dpi_for(HWND window) {
    using GetDpiForWindowFn = UINT(WINAPI*)(HWND);
    static GetDpiForWindowFn fn = []() -> GetDpiForWindowFn {
        HMODULE user32 = GetModuleHandleW(L"user32.dll");
        if (!user32) return nullptr;
        FARPROC proc = GetProcAddress(user32, "GetDpiForWindow");
        if (!proc) return nullptr;
        return reinterpret_cast<GetDpiForWindowFn>(
            reinterpret_cast<std::uintptr_t>(proc));
    }();
    if (fn) {
        UINT dpi = fn(window);
        if (dpi > 0) return static_cast<int>(dpi);
    }
    return 96;
}

std::string edit_text(HWND edit) {
    int len = GetWindowTextLengthW(edit);
    std::wstring wide(static_cast<size_t>(len) + 1, L'\0');
    GetWindowTextW(edit, wide.data(), len + 1);
    wide.resize(len);
    if (wide.empty()) return std::string();
    int utf8 = WideCharToMultiByte(CP_UTF8, 0, wide.data(), static_cast<int>(wide.size()),
        nullptr, 0, nullptr, nullptr);
    std::string out(static_cast<size_t>(utf8), '\0');
    if (utf8 > 0) {
        WideCharToMultiByte(CP_UTF8, 0, wide.data(), static_cast<int>(wide.size()),
            out.data(), utf8, nullptr, nullptr);
    }
    return out;
}

void set_edit_utf8(HWND edit, const std::string& text) {
    if (text.empty()) {
        SetWindowTextW(edit, L"");
        return;
    }
    int wide = MultiByteToWideChar(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()),
        nullptr, 0);
    std::wstring w(static_cast<size_t>(wide), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()),
        w.data(), wide);
    SetWindowTextW(edit, w.c_str());
}

HWND make_control(HWND parent, const wchar_t* cls, const wchar_t* text, DWORD style,
                  HMENU id, int x, int y, int w, int h, HFONT font) {
    HWND ctrl = CreateWindowExW(
        wcscmp(cls, L"EDIT") == 0 ? WS_EX_CLIENTEDGE : 0, cls, text,
        WS_CHILD | style, x, y, w, h, parent,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(
            reinterpret_cast<uintptr_t>(id))),
        GetModuleHandleW(nullptr), nullptr);
    SendMessageW(ctrl, WM_SETFONT, (WPARAM)font, TRUE);
    apply_control_theme(ctrl);
    return ctrl;
}

int checkbox_value(HWND hwnd, int id) {
    return SendMessageW(GetDlgItem(hwnd, id), BM_GETCHECK, 0, 0) == BST_CHECKED ? 1 : 0;
}

void set_checkbox(HWND hwnd, int id, bool value) {
    SendMessageW(GetDlgItem(hwnd, id), BM_SETCHECK,
        value ? BST_CHECKED : BST_UNCHECKED, 0);
}

int combo_selection(HWND hwnd, int id) {
    return static_cast<int>(SendMessageW(GetDlgItem(hwnd, id), CB_GETCURSEL, 0, 0));
}

void fill_combo(HWND hwnd, int id, const wchar_t* const* items, int count, int select) {
    HWND box = GetDlgItem(hwnd, id);
    SendMessageW(box, CB_RESETCONTENT, 0, 0);
    for (int i = 0; i < count; ++i) {
        SendMessageW(box, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(items[i]));
    }
    SendMessageW(box, CB_SETCURSEL, select, 0);
}

void fill_rect_color(HDC dc, const RECT& rect, COLORREF color) {
    HBRUSH brush = CreateSolidBrush(color);
    FillRect(dc, &rect, brush);
    DeleteObject(brush);
}

RECT client_rect_of(HWND hwnd) {
    RECT rect;
    GetClientRect(hwnd, &rect);
    return rect;
}

int sidebar_width(int dpi) {
    return MulDiv(198, dpi, 96);
}

int bottom_height(int dpi) {
    return MulDiv(48, dpi, 96);
}

RECT nav_item_rect(int index, int dpi) {
    int x = MulDiv(10, dpi, 96);
    int w = sidebar_width(dpi) - MulDiv(20, dpi, 96);
    int top = MulDiv(64, dpi, 96);
    int h = MulDiv(34, dpi, 96);
    int pitch = MulDiv(38, dpi, 96);
    return {x, top + index * pitch, x + w, top + index * pitch + h};
}

// Flashes "All changes saved" in the bottom bar.
void flash_saved(SettingsState* st) {
    st->saved_flash = true;
    SetTimer(st->hwnd, kFlashTimer, 1600, nullptr);
    InvalidateRect(st->hwnd, nullptr, TRUE);
}

void load_into_controls(SettingsState* st) {
    Runtime* rt = runtime();
    if (!rt) return;
    const mustela::Settings& s = rt->settings;

    static const wchar_t* const kStyles[] = {L"Classic", L"Zen"};
    fill_combo(st->hwnd, ID_STYLE, kStyles, 2, st->original_style == "zen" ? 1 : 0);

    static const wchar_t* const kSides[] = {L"Left", L"Right"};
    fill_combo(st->hwnd, ID_RAILSIDE, kSides, 2,
               st->original_rail_side == "right" ? 1 : 0);
    EnableWindow(GetDlgItem(st->hwnd, ID_RAILSIDE),
                 st->original_style == "zen");

    static const wchar_t* const kBackdrops[] = {
        L"Off (solid chrome)", L"Mica", L"Acrylic (frosted glass)"};
    fill_combo(st->hwnd, ID_BACKDROP, kBackdrops, 3,
               st->original_backdrop == "acrylic" ? 2
                   : (st->original_backdrop == "mica" ? 1 : 0));

    set_edit_utf8(GetDlgItem(st->hwnd, ID_HOMEPAGE), s.homepage);
    set_edit_utf8(GetDlgItem(st->hwnd, ID_SEARCH), s.search_template);
    set_edit_utf8(GetDlgItem(st->hwnd, ID_DOH), s.dns_over_https);

    static const wchar_t* const kCookies[] = {
        L"Allow all", L"Block third-party", L"Block all"};
    fill_combo(st->hwnd, ID_COOKIES, kCookies, 3,
               s.cookie_policy == "always" ? 0
                   : (s.cookie_policy == "never" ? 2 : 1));

    set_checkbox(st->hwnd, ID_JAVASCRIPT, s.javascript_enabled);
    set_checkbox(st->hwnd, ID_AUTOPLAY, s.block_autoplay);
    set_checkbox(st->hwnd, ID_GPC, s.gpc);
    set_checkbox(st->hwnd, ID_AUDITING, s.hyperlink_auditing);
    set_checkbox(st->hwnd, ID_FP_WEBGL, s.fingerprint_webgl);
    set_checkbox(st->hwnd, ID_FP_CANVAS, s.fingerprint_canvas);
    set_checkbox(st->hwnd, ID_PRIVATE, s.private_tabs_enabled);
    set_checkbox(st->hwnd, ID_START_PRIVATE, s.start_in_private);
    set_checkbox(st->hwnd, ID_SESSION, s.session_restore);
    set_checkbox(st->hwnd, ID_HISTORY_SAVE, s.history_enabled);
}

// Reads every control and persists both stores. Style-affecting changes fire
// the shell callback once; the settings window re-skins itself too.
void apply_all(SettingsState* st) {
    Runtime* rt = runtime();
    if (!rt) return;
    mustela::Settings& s = rt->settings;

    std::string style = combo_selection(st->hwnd, ID_STYLE) == 1 ? "zen" : "classic";
    std::string rail = combo_selection(st->hwnd, ID_RAILSIDE) == 1 ? "right" : "left";
    std::string backdrop = combo_selection(st->hwnd, ID_BACKDROP) == 2 ? "acrylic"
                         : (combo_selection(st->hwnd, ID_BACKDROP) == 1 ? "mica" : "off");

    s.homepage = edit_text(GetDlgItem(st->hwnd, ID_HOMEPAGE));
    s.search_template = edit_text(GetDlgItem(st->hwnd, ID_SEARCH));
    s.dns_over_https = edit_text(GetDlgItem(st->hwnd, ID_DOH));

    int cookies = combo_selection(st->hwnd, ID_COOKIES);
    s.cookie_policy = cookies == 0 ? "always" : (cookies == 2 ? "never"
                                                              : "no-third-party");

    s.javascript_enabled = checkbox_value(st->hwnd, ID_JAVASCRIPT) != 0;
    s.block_autoplay = checkbox_value(st->hwnd, ID_AUTOPLAY) != 0;
    s.gpc = checkbox_value(st->hwnd, ID_GPC) != 0;
    s.hyperlink_auditing = checkbox_value(st->hwnd, ID_AUDITING) != 0;
    s.fingerprint_webgl = checkbox_value(st->hwnd, ID_FP_WEBGL) != 0;
    s.fingerprint_canvas = checkbox_value(st->hwnd, ID_FP_CANVAS) != 0;
    s.private_tabs_enabled = checkbox_value(st->hwnd, ID_PRIVATE) != 0;
    s.start_in_private = checkbox_value(st->hwnd, ID_START_PRIVATE) != 0;
    s.session_restore = checkbox_value(st->hwnd, ID_SESSION) != 0;
    s.history_enabled = checkbox_value(st->hwnd, ID_HISTORY_SAVE) != 0;

    mustela::save_settings(rt->settings_path, s);
    rt->policy = mustela::privacy_from(s);
    rt->policy.private_tabs_available = s.private_tabs_enabled;
    rt->blocklist = mustela::default_blocklist(
        rt->policy.blocklist ? s.blocklist_file : std::string());

    kiwi::save_shell_style(rt->config_dir, style);
    kiwi::save_shell_backdrop(rt->config_dir, backdrop);
    kiwi::save_shell_rail_side(rt->config_dir, rail);
    kiwi::save_shell_theme(rt->config_dir, st->theme);
    kiwi::save_shell_accent(rt->config_dir, st->accent);

    bool shell_changed = style != st->original_style
                      || backdrop != st->original_backdrop
                      || rail != st->original_rail_side
                      || st->theme != st->original_theme
                      || st->accent != st->original_accent;

    st->original_style = style;
    st->original_backdrop = backdrop;
    st->original_rail_side = rail;
    st->original_theme = st->theme;
    st->original_accent = st->accent;

    EnableWindow(GetDlgItem(st->hwnd, ID_RAILSIDE), style == "zen");

    if (shell_changed && st->on_style_changed) {
        st->on_style_changed();
    }
    // The settings window itself follows the theme it just applied.
    retheme_children(st->hwnd);
    InvalidateRect(st->hwnd, nullptr, TRUE);
    flash_saved(st);
}

// ---- layout --------------------------------------------------------------

struct PageSpace {
    int x0;
    int y0;
    int w;
};

void place(HWND ctrl, int x, int y, int w, int h) {
    MoveWindow(ctrl, x, y, w, h, TRUE);
}

void layout_page(SettingsState* st, const PageSpace& page) {
    int dpi = dpi_for(st->hwnd);
    int label_w = MulDiv(130, dpi, 96);
    int edit_h = MulDiv(24, dpi, 96);
    int combo_h = MulDiv(24, dpi, 96);
    // Combo window height = closed box + the dropped list; the closed box
    // sizes itself from the font, the rest is dropdown area.
    int combo_drop = combo_h + MulDiv(110, dpi, 96);
    int check_h = MulDiv(20, dpi, 96);
    int btn_h = MulDiv(28, dpi, 96);
    int row = MulDiv(30, dpi, 96);
    int half = (page.w - MulDiv(8, dpi, 96)) / 2;
    int chip_w = MulDiv(86, dpi, 96);
    int chip_h = MulDiv(52, dpi, 96);
    int chip_gap = MulDiv(8, dpi, 96);

    int y = page.y0;
    switch (st->section) {
        case SECTION_GENERAL: {
            place(GetDlgItem(st->hwnd, ID_HOMEPAGE_LABEL), page.x0, y + 3,
                  label_w, check_h);
            place(GetDlgItem(st->hwnd, ID_HOMEPAGE), page.x0 + label_w, y,
                  page.w - label_w, edit_h);
            y += row + MulDiv(8, dpi, 96);
            place(GetDlgItem(st->hwnd, ID_SESSION), page.x0, y, page.w, check_h);
            y += MulDiv(24, dpi, 96);
            place(GetDlgItem(st->hwnd, ID_HISTORY_SAVE), page.x0, y, page.w, check_h);
            y += MulDiv(24, dpi, 96);
            place(GetDlgItem(st->hwnd, ID_PRIVATE), page.x0, y, page.w, check_h);
            y += MulDiv(24, dpi, 96);
            place(GetDlgItem(st->hwnd, ID_START_PRIVATE), page.x0, y, page.w, check_h);
            y += row;
            break;
        }
        case SECTION_APPEARANCE: {
            place(GetDlgItem(st->hwnd, ID_STYLE_LABEL), page.x0, y + 3, label_w, check_h);
            place(GetDlgItem(st->hwnd, ID_STYLE), page.x0 + label_w, y,
                  MulDiv(180, dpi, 96), combo_drop);
            y += row + MulDiv(6, dpi, 96);
            place(GetDlgItem(st->hwnd, ID_RAILSIDE_LABEL), page.x0, y + 3, label_w, check_h);
            place(GetDlgItem(st->hwnd, ID_RAILSIDE), page.x0 + label_w, y,
                  MulDiv(180, dpi, 96), combo_drop);
            y += row + MulDiv(6, dpi, 96);
            place(GetDlgItem(st->hwnd, ID_BACKDROP_LABEL), page.x0, y + 3, label_w, check_h);
            place(GetDlgItem(st->hwnd, ID_BACKDROP), page.x0 + label_w, y,
                  MulDiv(260, dpi, 96), combo_drop);
            y += row + MulDiv(10, dpi, 96);
            // Theme chips: 4 per row over the page width.
            int per_row = std::max(1, (page.w + chip_gap) / (chip_w + chip_gap));
            for (int i = 0; i < kThemeChipCount; ++i) {
                int cx = page.x0 + (i % per_row) * (chip_w + chip_gap);
                int cy = y + (i / per_row) * (chip_h + chip_gap);
                place(GetDlgItem(st->hwnd, ID_THEME_BASE + i), cx, cy, chip_w, chip_h);
            }
            int rows = (kThemeChipCount + per_row - 1) / per_row;
            y += rows * (chip_h + chip_gap) + MulDiv(6, dpi, 96);
            place(GetDlgItem(st->hwnd, ID_ACCENT), page.x0, y, half, btn_h);
            place(GetDlgItem(st->hwnd, ID_ACCENT_RESET), page.x0 + half + MulDiv(8, dpi, 96),
                  y, half, btn_h);
            y += row;
            break;
        }
        case SECTION_SEARCH: {
            place(GetDlgItem(st->hwnd, ID_SEARCH_LABEL), page.x0, y + 3, label_w, check_h);
            place(GetDlgItem(st->hwnd, ID_SEARCH), page.x0 + label_w, y,
                  page.w - label_w, edit_h);
            y += row + MulDiv(8, dpi, 96);
            place(GetDlgItem(st->hwnd, ID_DOH_LABEL), page.x0, y + 3, label_w, check_h);
            place(GetDlgItem(st->hwnd, ID_DOH), page.x0 + label_w, y,
                  page.w - label_w, edit_h);
            y += row + MulDiv(8, dpi, 96);
            place(GetDlgItem(st->hwnd, ID_ENGINES), page.x0, y, half, btn_h);
            y += row;
            break;
        }
        case SECTION_PRIVACY: {
            place(GetDlgItem(st->hwnd, ID_COOKIES_LABEL), page.x0, y + 3, label_w, check_h);
            place(GetDlgItem(st->hwnd, ID_COOKIES), page.x0 + label_w, y,
                  MulDiv(220, dpi, 96), combo_drop);
            y += row + MulDiv(6, dpi, 96);
            place(GetDlgItem(st->hwnd, ID_JAVASCRIPT), page.x0, y, page.w, check_h);
            y += MulDiv(24, dpi, 96);
            place(GetDlgItem(st->hwnd, ID_AUTOPLAY), page.x0, y, page.w, check_h);
            y += MulDiv(24, dpi, 96);
            place(GetDlgItem(st->hwnd, ID_GPC), page.x0, y, page.w, check_h);
            y += MulDiv(24, dpi, 96);
            place(GetDlgItem(st->hwnd, ID_AUDITING), page.x0, y, page.w, check_h);
            y += MulDiv(24, dpi, 96);
            place(GetDlgItem(st->hwnd, ID_FP_WEBGL), page.x0, y, page.w, check_h);
            y += MulDiv(24, dpi, 96);
            place(GetDlgItem(st->hwnd, ID_FP_CANVAS), page.x0, y, page.w, check_h);
            y += row;
            break;
        }
        case SECTION_DATA: {
            place(GetDlgItem(st->hwnd, ID_SITERULES), page.x0, y, half, btn_h);
            place(GetDlgItem(st->hwnd, ID_HISTORY), page.x0 + half + MulDiv(8, dpi, 96),
                  y, half, btn_h);
            y += row + MulDiv(4, dpi, 96);
            place(GetDlgItem(st->hwnd, ID_CLEARDATA), page.x0, y, half, btn_h);
            place(GetDlgItem(st->hwnd, ID_UPDATE), page.x0 + half + MulDiv(8, dpi, 96),
                  y, half, btn_h);
            y += row;
            break;
        }
        default:
            break;
    }
}

void show_section(SettingsState* st, int section) {
    st->section = section;
    for (int i = 0; i < SECTION_COUNT; ++i) {
        bool visible = i == section;
        for (HWND ctrl : st->pages[i]) {
            ShowWindow(ctrl, visible ? SW_SHOW : SW_HIDE);
        }
    }
    RECT client = client_rect_of(st->hwnd);
    int dpi = dpi_for(st->hwnd);
    int margin = MulDiv(24, dpi, 96);
    PageSpace page;
    page.x0 = sidebar_width(dpi) + margin;
    page.y0 = MulDiv(64, dpi, 96);
    page.w = client.right - page.x0 - margin;
    layout_page(st, page);
    InvalidateRect(st->hwnd, nullptr, TRUE);
}

void relayout(SettingsState* st) {
    show_section(st, st->section);
    RECT client = client_rect_of(st->hwnd);
    int dpi = dpi_for(st->hwnd);
    int btn_w = MulDiv(88, dpi, 96);
    int btn_h = MulDiv(28, dpi, 96);
    int margin = MulDiv(20, dpi, 96);
    place(GetDlgItem(st->hwnd, ID_CLOSEWIN),
          client.right - margin - btn_w,
          client.bottom - bottom_height(dpi) + (bottom_height(dpi) - btn_h) / 2,
          btn_w, btn_h);
}

// ---- painting ------------------------------------------------------------

void paint_sidebar(HDC dc, SettingsState* st, const RECT& client) {
    int dpi = dpi_for(st->hwnd);
    const gfx::AuxTheme& t = gfx::aux_theme();
    RECT side = {0, 0, sidebar_width(dpi), client.bottom};
    COLORREF tint = gfx::blend(t.surface, t.ink, gfx::dark_chrome() ? 14 : 5);
    fill_rect_color(dc, side, tint);

    SetBkMode(dc, TRANSPARENT);

    // Sidebar caption.
    RECT brand = {MulDiv(16, dpi, 96), MulDiv(18, dpi, 96),
                  sidebar_width(dpi) - MulDiv(12, dpi, 96), MulDiv(44, dpi, 96)};
    SelectObject(dc, st->title);
    SetTextColor(dc, t.ink);
    DrawTextW(dc, L"Settings", -1, &brand,
        DT_SINGLELINE | DT_LEFT | DT_VCENTER);

    for (int i = 0; i < SECTION_COUNT; ++i) {
        RECT item = nav_item_rect(i, dpi);
        bool selected = st->section == i;
        bool hovered = st->hover_section == i && !selected;
        if (selected) {
            COLORREF pill = gfx::blend(t.surface, gfx::palette().accent, 30);
            gfx::fill_round_rect(dc, item, MulDiv(8, dpi, 96), dpi, pill);
        } else if (hovered) {
            COLORREF wash = gfx::blend(t.surface, t.ink, gfx::dark_chrome() ? 10 : 4);
            gfx::fill_round_rect(dc, item, MulDiv(8, dpi, 96), dpi, wash);
        }
        RECT glyph_box = {item.left + MulDiv(10, dpi, 96),
                          (item.top + item.bottom) / 2 - MulDiv(9, dpi, 96),
                          item.left + MulDiv(28, dpi, 96),
                          (item.top + item.bottom) / 2 + MulDiv(9, dpi, 96)};
        COLORREF ink = selected ? gfx::palette().accent : t.muted;
        if (hovered) ink = t.ink;
        gfx::draw_glyph(dc, glyph_box, kSections[i].glyph, ink, 1.5f, dpi);
        RECT text = {item.left + MulDiv(38, dpi, 96), item.top, item.right, item.bottom};
        SelectObject(dc, selected ? st->bold : st->font);
        SetTextColor(dc, selected ? t.ink : (hovered ? t.ink : t.muted));
        DrawTextW(dc, kSections[i].nav, -1, &text,
            DT_SINGLELINE | DT_LEFT | DT_VCENTER);
    }
}

void paint_page_chrome(HDC dc, SettingsState* st, const RECT& client) {
    int dpi = dpi_for(st->hwnd);
    const gfx::AuxTheme& t = gfx::aux_theme();
    SetBkMode(dc, TRANSPARENT);

    // Page heading.
    int margin = MulDiv(24, dpi, 96);
    RECT head = {sidebar_width(dpi) + margin, MulDiv(20, dpi, 96),
                 client.right - margin, MulDiv(48, dpi, 96)};
    SelectObject(dc, st->title);
    SetTextColor(dc, t.ink);
    DrawTextW(dc, kSections[st->section].heading, -1, &head,
        DT_SINGLELINE | DT_LEFT | DT_VCENTER);

    // Hairline under the heading and above the bottom bar.
    RECT rule = {sidebar_width(dpi) + margin, MulDiv(56, dpi, 96),
                 client.right - margin, MulDiv(56, dpi, 96) + MulDiv(1, dpi, 96)};
    fill_rect_color(dc, rule, t.outline);
    RECT bar_rule = {0, client.bottom - bottom_height(dpi), client.right,
                     client.bottom - bottom_height(dpi) + MulDiv(1, dpi, 96)};
    fill_rect_color(dc, bar_rule, t.outline);

    // Bottom bar status.
    RECT status = {sidebar_width(dpi) + margin, client.bottom - bottom_height(dpi),
                   client.right - MulDiv(120, dpi, 96), client.bottom};
    SelectObject(dc, st->font);
    SetTextColor(dc, st->saved_flash ? t.ink : t.muted);
    DrawTextW(dc, st->saved_flash ? L"All changes saved"
                                  : L"Changes apply instantly",
              -1, &status, DT_SINGLELINE | DT_LEFT | DT_VCENTER);
}

void paint_theme_chip(HDC dc, const RECT& rect, int index, const SettingsState* st) {
    int dpi = dpi_for(st->hwnd);
    const gfx::AuxTheme& t = gfx::aux_theme();
    const std::vector<std::string>& names = gfx::theme_names();
    if (index < 0 || index >= static_cast<int>(names.size())) return;
    const std::string& name = names[static_cast<size_t>(index)];

    COLORREF bg = 0;
    COLORREF text = 0;
    COLORREF accent = 0;
    if (!gfx::theme_preview(name, &bg, &text, &accent)) return;

    bool selected = st->theme == name;
    // Chip sits on the dialog surface; draw the preview card on top.
    RECT card = {rect.left + MulDiv(6, dpi, 96), rect.top + MulDiv(4, dpi, 96),
                 rect.right - MulDiv(6, dpi, 96), rect.top + MulDiv(28, dpi, 96)};
    fill_rect_color(dc, rect, t.surface);
    gfx::fill_round_rect(dc, card, MulDiv(6, dpi, 96), dpi, bg);
    if (selected) {
        gfx::stroke_round_rect(dc, card, MulDiv(6, dpi, 96), dpi,
                               gfx::palette().accent, 2.0f);
        HBRUSH ring = CreateSolidBrush(gfx::palette().accent);
        RECT dot = {card.right - MulDiv(10, dpi, 96), card.bottom - MulDiv(10, dpi, 96),
                    card.right - MulDiv(4, dpi, 96), card.bottom - MulDiv(4, dpi, 96)};
        HBRUSH old = static_cast<HBRUSH>(SelectObject(dc, ring));
        Ellipse(dc, dot.left, dot.top, dot.right, dot.bottom);
        SelectObject(dc, old);
        DeleteObject(ring);
    } else {
        gfx::stroke_round_rect(dc, card, MulDiv(6, dpi, 96), dpi, t.outline, 1.0f);
    }

    RECT label = {rect.left, rect.top + MulDiv(30, dpi, 96), rect.right, rect.bottom};
    SelectObject(dc, selected ? st->bold : st->small);
    SetTextColor(dc, selected ? t.ink : t.muted);
    std::wstring wide(name.begin(), name.end());
    DrawTextW(dc, wide.c_str(), -1, &label, DT_SINGLELINE | DT_CENTER | DT_VCENTER);
}

// ---- wndproc -------------------------------------------------------------

int nav_hit(const SettingsState* st, const POINT& cl) {
    int dpi = dpi_for(st->hwnd);
    for (int i = 0; i < SECTION_COUNT; ++i) {
        RECT item = nav_item_rect(i, dpi);
        item.left = 0;
        item.right = sidebar_width(dpi);
        if (PtInRect(&item, cl)) return i;
    }
    return -1;
}

void track_mouse(HWND hwnd) {
    TRACKMOUSEEVENT track = {sizeof(track), TME_LEAVE, hwnd, 0};
    TrackMouseEvent(&track);
}

LRESULT CALLBACK wnd_proc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
    switch (msg) {
        case WM_CREATE: {
            auto* create = reinterpret_cast<CREATESTRUCTW*>(lparam);
            SetWindowLongPtrW(hwnd, GWLP_USERDATA,
                reinterpret_cast<LONG_PTR>(create->lpCreateParams));
            return 0;
        }
        case WM_DESTROY: {
            SettingsState* st = state_for(hwnd);
            if (st) {
                apply_all(st);  // flush edits that never lost focus
                KillTimer(hwnd, kFlashTimer);
                st->hwnd = nullptr;
                DeleteObject(st->font);
                DeleteObject(st->bold);
                DeleteObject(st->title);
                DeleteObject(st->small);
                delete st;
                SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
            }
            return 0;
        }
        case WM_ERASEBKGND:
            return aux_erasebkgnd(hwnd, wparam);
        case WM_GETMINMAXINFO: {
            auto* info = reinterpret_cast<MINMAXINFO*>(lparam);
            info->ptMinTrackSize = {MulDiv(680, dpi_for(hwnd), 96),
                                    MulDiv(520, dpi_for(hwnd), 96)};
            return 0;
        }
        case WM_TIMER:
            if (wparam == kFlashTimer) {
                KillTimer(hwnd, kFlashTimer);
                if (SettingsState* st = state_for(hwnd)) {
                    st->saved_flash = false;
                    InvalidateRect(hwnd, nullptr, TRUE);
                }
            }
            return 0;
        case WM_CTLCOLORSTATIC:
            return aux_static_ctlcolor(reinterpret_cast<HDC>(wparam));
        case WM_CTLCOLOREDIT:
        case WM_CTLCOLORLISTBOX:
            return aux_field_ctlcolor(reinterpret_cast<HDC>(wparam));
        case WM_DRAWITEM: {
            auto* draw = reinterpret_cast<DRAWITEMSTRUCT*>(lparam);
            if (draw->CtlType == ODT_BUTTON && draw->CtlID >= ID_THEME_BASE
                && draw->CtlID < ID_THEME_BASE + kThemeChipCount) {
                if (SettingsState* st = state_for(hwnd)) {
                    paint_theme_chip(draw->hDC, draw->rcItem,
                                     draw->CtlID - ID_THEME_BASE, st);
                }
                return TRUE;
            }
            break;
        }
        default:
            break;
    }

    SettingsState* st = state_for(hwnd);
    if (!st) return DefWindowProcW(hwnd, msg, wparam, lparam);

    switch (msg) {
        case kSyncRailSideMsg: {
            Runtime* rt = runtime();
            if (rt) {
                st->original_rail_side = kiwi::load_shell_rail_side(rt->config_dir);
                SendMessageW(GetDlgItem(hwnd, ID_RAILSIDE), CB_SETCURSEL,
                             st->original_rail_side == "right" ? 1 : 0, 0);
            }
            return 0;
        }
        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC dc = BeginPaint(hwnd, &ps);
            RECT client = client_rect_of(hwnd);
            paint_sidebar(dc, st, client);
            paint_page_chrome(dc, st, client);
            EndPaint(hwnd, &ps);
            return 0;
        }
        case WM_MOUSEMOVE: {
            POINT cl = {GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam)};
            int hit = nav_hit(st, cl);
            if (hit != st->hover_section) {
                st->hover_section = hit;
                InvalidateRect(hwnd, nullptr, FALSE);
            }
            if (hit >= 0) track_mouse(hwnd);
            return 0;
        }
        case WM_MOUSELEAVE:
            if (st->hover_section != -1) {
                st->hover_section = -1;
                InvalidateRect(hwnd, nullptr, FALSE);
            }
            return 0;
        case WM_LBUTTONDOWN: {
            POINT cl = {GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam)};
            int hit = nav_hit(st, cl);
            if (hit >= 0 && hit != st->section) {
                show_section(st, hit);
            }
            return 0;
        }
        case WM_SIZE:
            relayout(st);
            return 0;
        case WM_COMMAND: {
            int id = LOWORD(wparam);
            int code = HIWORD(wparam);
            switch (id) {
                case ID_STYLE:
                case ID_RAILSIDE:
                case ID_BACKDROP:
                case ID_COOKIES:
                    if (code == CBN_SELCHANGE) apply_all(st);
                    return 0;
                case ID_JAVASCRIPT:
                case ID_AUTOPLAY:
                case ID_GPC:
                case ID_AUDITING:
                case ID_FP_WEBGL:
                case ID_FP_CANVAS:
                case ID_PRIVATE:
                case ID_START_PRIVATE:
                case ID_SESSION:
                case ID_HISTORY_SAVE:
                    if (code == BN_CLICKED) apply_all(st);
                    return 0;
                case ID_HOMEPAGE:
                case ID_SEARCH:
                case ID_DOH:
                    if (code == EN_KILLFOCUS) apply_all(st);
                    return 0;
                case ID_ACCENT: {
                    COLORREF custom = gfx::palette().accent;
                    CHOOSECOLORW cc = {};
                    static COLORREF customs[16] = {};
                    cc.lStructSize = sizeof(cc);
                    cc.hwndOwner = hwnd;
                    cc.rgbResult = custom;
                    cc.lpCustColors = customs;
                    cc.Flags = CC_FULLOPEN | CC_RGBINIT;
                    if (ChooseColorW(&cc)) {
                        wchar_t hex[16];
                        swprintf(hex, 16, L"#%02X%02X%02X",
                                 GetRValue(cc.rgbResult),
                                 GetGValue(cc.rgbResult),
                                 GetBValue(cc.rgbResult));
                        int utf8 = WideCharToMultiByte(CP_UTF8, 0, hex, -1,
                            nullptr, 0, nullptr, nullptr);
                        std::string out(static_cast<size_t>(utf8) - 1, '\0');
                        WideCharToMultiByte(CP_UTF8, 0, hex, -1, out.data(),
                            utf8, nullptr, nullptr);
                        st->accent = out;
                        apply_all(st);
                    }
                    return 0;
                }
                case ID_ACCENT_RESET:
                    st->accent.clear();
                    apply_all(st);
                    return 0;
                case ID_ENGINES:
                    open_search_engines_window(st->hwnd);
                    return 0;
                case ID_SITERULES:
                    open_site_rules_dialog(st->hwnd);
                    return 0;
                case ID_HISTORY:
                    open_history_window(shell_main_hwnd(),
                        [](const std::string& url) { shell_open_url(url); });
                    return 0;
                case ID_CLEARDATA:
                    open_clear_data_window(st->hwnd);
                    return 0;
                case ID_UPDATE:
                    check_for_updates(hwnd);
                    return 0;
                case ID_CLOSEWIN:
                    DestroyWindow(hwnd);
                    return 0;
                default:
                    if (id >= ID_THEME_BASE && id < ID_THEME_BASE + kThemeChipCount) {
                        const std::vector<std::string>& names = gfx::theme_names();
                        int index = id - ID_THEME_BASE;
                        if (index >= 0 && index < static_cast<int>(names.size())) {
                            st->theme = names[static_cast<size_t>(index)];
                            apply_all(st);
                        }
                        return 0;
                    }
                    break;
            }
            return 0;
        }
        default:
            break;
    }
    return DefWindowProcW(hwnd, msg, wparam, lparam);
}

// ---- construction --------------------------------------------------------

HWND add_label(SettingsState* st, int id, int section, const wchar_t* text, HFONT font) {
    HWND ctrl = make_control(st->hwnd, L"STATIC", text, 0,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), 0, 0, 10, 10, font);
    st->pages[section].push_back(ctrl);
    return ctrl;
}

HWND add_item(SettingsState* st, int id, int section, const wchar_t* cls,
              const wchar_t* text, DWORD style) {
    HWND ctrl = make_control(st->hwnd, cls, text,
        style | WS_TABSTOP, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
        0, 0, 10, 10, st->font);
    st->pages[section].push_back(ctrl);
    return ctrl;
}

}  // namespace

void sync_settings_rail_side() {
    EnumWindows([](HWND hwnd, LPARAM) -> BOOL {
        wchar_t cls[32];
        if (GetClassNameW(hwnd, cls, 32) && wcscmp(cls, kClass) == 0) {
            SendMessageW(hwnd, kSyncRailSideMsg, 0, 0);
        }
        return TRUE;
    }, 0);
}

void open_settings_window(HWND owner, std::function<void()> on_style_changed) {
    static bool registered = false;
    if (!registered) {
        WNDCLASSW wc = {};
        wc.lpfnWndProc = wnd_proc;
        wc.hInstance = GetModuleHandleW(nullptr);
        wc.hCursor = LoadCursorW(nullptr, reinterpret_cast<LPCWSTR>(IDC_ARROW));
        wc.hbrBackground = nullptr;  // WM_ERASEBKGND paints the live theme
        wc.lpszClassName = kClass;
        RegisterClassW(&wc);
        registered = true;
    }

    Runtime* rt = runtime();
    if (!rt) return;

    HWND existing = FindWindowExW(owner, nullptr, kClass, nullptr);
    if (existing) {
        ShowWindow(existing, SW_RESTORE);
        SetForegroundWindow(existing);
        return;
    }

    auto* st = new SettingsState();
    st->on_style_changed = std::move(on_style_changed);
    st->original_style = kiwi::load_shell_style(rt->config_dir);
    st->original_backdrop = kiwi::load_shell_backdrop(rt->config_dir);
    st->original_theme = kiwi::load_shell_theme(rt->config_dir);
    st->original_accent = kiwi::load_shell_accent(rt->config_dir);
    st->original_rail_side = kiwi::load_shell_rail_side(rt->config_dir);
    st->theme = st->original_theme;
    st->accent = st->original_accent;

    RECT area = {0, 0, 800, 600};
    AdjustWindowRect(&area, WS_OVERLAPPEDWINDOW, FALSE);
    st->hwnd = CreateWindowExW(0, kClass, L"Kiwi settings",
        WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT,
        area.right - area.left, area.bottom - area.top,
        owner, nullptr, GetModuleHandleW(nullptr), st);
    if (!st->hwnd) {
        delete st;
        return;
    }

    int dpi = dpi_for(st->hwnd);
    st->font = CreateFontW(-MulDiv(10, dpi, 72), 0, 0, 0, FW_NORMAL, FALSE, FALSE,
        FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
    st->bold = CreateFontW(-MulDiv(10, dpi, 72), 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE,
        FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
    st->title = CreateFontW(-MulDiv(17, dpi, 72), 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE,
        FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
    st->small = CreateFontW(-MulDiv(8, dpi, 72), 0, 0, 0, FW_NORMAL, FALSE, FALSE,
        FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");

    // General
    add_label(st, ID_HOMEPAGE_LABEL, SECTION_GENERAL, L"Homepage", st->font);
    add_item(st, ID_HOMEPAGE, SECTION_GENERAL, L"EDIT", nullptr, ES_AUTOHSCROLL);
    add_item(st, ID_SESSION, SECTION_GENERAL, L"BUTTON",
        L"Restore session on startup", BS_AUTOCHECKBOX | BS_TEXT);
    add_item(st, ID_HISTORY_SAVE, SECTION_GENERAL, L"BUTTON",
        L"Remember browsing history", BS_AUTOCHECKBOX | BS_TEXT);
    add_item(st, ID_PRIVATE, SECTION_GENERAL, L"BUTTON",
        L"Private tabs available (Ctrl+Shift+P)", BS_AUTOCHECKBOX | BS_TEXT);
    add_item(st, ID_START_PRIVATE, SECTION_GENERAL, L"BUTTON",
        L"Start in private mode", BS_AUTOCHECKBOX | BS_TEXT);

    // Appearance
    add_label(st, ID_STYLE_LABEL, SECTION_APPEARANCE, L"Shell style", st->font);
    add_item(st, ID_STYLE, SECTION_APPEARANCE, L"COMBOBOX", nullptr,
             CBS_DROPDOWNLIST);
    add_label(st, ID_RAILSIDE_LABEL, SECTION_APPEARANCE, L"Panel side", st->font);
    add_item(st, ID_RAILSIDE, SECTION_APPEARANCE, L"COMBOBOX", nullptr,
             CBS_DROPDOWNLIST);
    add_label(st, ID_BACKDROP_LABEL, SECTION_APPEARANCE, L"Chrome backdrop", st->font);
    add_item(st, ID_BACKDROP, SECTION_APPEARANCE, L"COMBOBOX", nullptr,
             CBS_DROPDOWNLIST);
    for (int i = 0; i < kThemeChipCount; ++i) {
        add_item(st, ID_THEME_BASE + i, SECTION_APPEARANCE, L"BUTTON", nullptr,
                 BS_OWNERDRAW);
    }
    add_item(st, ID_ACCENT, SECTION_APPEARANCE, L"BUTTON", L"Custom accent...",
             BS_PUSHBUTTON);
    add_item(st, ID_ACCENT_RESET, SECTION_APPEARANCE, L"BUTTON",
             L"Reset accent", BS_PUSHBUTTON);

    // Search
    add_label(st, ID_SEARCH_LABEL, SECTION_SEARCH, L"Search template (%s)", st->font);
    add_item(st, ID_SEARCH, SECTION_SEARCH, L"EDIT", nullptr, ES_AUTOHSCROLL);
    add_label(st, ID_DOH_LABEL, SECTION_SEARCH, L"DNS over HTTPS", st->font);
    add_item(st, ID_DOH, SECTION_SEARCH, L"EDIT", nullptr, ES_AUTOHSCROLL);
    add_item(st, ID_ENGINES, SECTION_SEARCH, L"BUTTON", L"Search engines...",
             BS_PUSHBUTTON);

    // Privacy
    add_label(st, ID_COOKIES_LABEL, SECTION_PRIVACY, L"Cookie policy", st->font);
    add_item(st, ID_COOKIES, SECTION_PRIVACY, L"COMBOBOX", nullptr,
             CBS_DROPDOWNLIST);
    add_item(st, ID_JAVASCRIPT, SECTION_PRIVACY, L"BUTTON", L"Enable JavaScript",
             BS_AUTOCHECKBOX | BS_TEXT);
    add_item(st, ID_AUTOPLAY, SECTION_PRIVACY, L"BUTTON",
        L"Block autoplay (require user gesture)", BS_AUTOCHECKBOX | BS_TEXT);
    add_item(st, ID_GPC, SECTION_PRIVACY, L"BUTTON",
        L"Global Privacy Control (Sec-GPC)", BS_AUTOCHECKBOX | BS_TEXT);
    add_item(st, ID_AUDITING, SECTION_PRIVACY, L"BUTTON",
        L"Allow hyperlink auditing (<a ping>)", BS_AUTOCHECKBOX | BS_TEXT);
    add_item(st, ID_FP_WEBGL, SECTION_PRIVACY, L"BUTTON",
        L"Fingerprint protection: WebGL vendor", BS_AUTOCHECKBOX | BS_TEXT);
    add_item(st, ID_FP_CANVAS, SECTION_PRIVACY, L"BUTTON",
        L"Fingerprint protection: canvas", BS_AUTOCHECKBOX | BS_TEXT);

    // Data
    add_item(st, ID_SITERULES, SECTION_DATA, L"BUTTON", L"Per-site rules...",
             BS_PUSHBUTTON);
    add_item(st, ID_HISTORY, SECTION_DATA, L"BUTTON", L"Browsing history...",
             BS_PUSHBUTTON);
    add_item(st, ID_CLEARDATA, SECTION_DATA, L"BUTTON", L"Clear browsing data...",
             BS_PUSHBUTTON);
    add_item(st, ID_UPDATE, SECTION_DATA, L"BUTTON", L"Check for updates...",
             BS_PUSHBUTTON);

    // Bottom bar (always visible, not part of any page).
    make_control(st->hwnd, L"BUTTON", L"Close",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(ID_CLOSEWIN)),
        0, 0, 10, 10, st->font);

    load_into_controls(st);
    show_section(st, SECTION_GENERAL);
    ShowWindow(st->hwnd, SW_SHOW);
    UpdateWindow(st->hwnd);
}

}  // namespace kiwi
