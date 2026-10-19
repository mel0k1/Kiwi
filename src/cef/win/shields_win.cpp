// Kiwi Shields exceptions editor — the same dialog language as the
// workspaces manager: owner-drawn list of sites with lowered shields, an
// edit + Add button, Remove. Backed by <config>/shields.toml through the
// shared store; rt->shields_cfg is updated in place.

#include "shields_win.hpp"

#include "../cef_runtime.hpp"
#include "../../shields_rules.hpp"
#include "../../shields_store.hpp"
#include "aux_theme_win.hpp"
#include "gfx_win.hpp"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <windowsx.h>
#include <commctrl.h>

#include <string>
#include <vector>

namespace kiwi {

namespace {

constexpr wchar_t kClass[] = L"KiwiShieldsWindow";

constexpr int ID_LIST = 101;
constexpr int ID_HOST = 102;
constexpr int ID_ADD = 103;
constexpr int ID_REMOVE = 104;
constexpr int ID_CLEAR = 105;
constexpr int ID_CLOSE = 106;
constexpr int ID_HINT = 107;

struct ShUi {
    HWND hwnd = nullptr;
    HWND list = nullptr;
    HWND host = nullptr;
    shields::ShieldsConfig config;
    HFONT font = nullptr;
};

ShUi* state_for(HWND hwnd) {
    return reinterpret_cast<ShUi*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
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

void fill_rect_color(HDC dc, const RECT& rect, COLORREF color) {
    HBRUSH brush = CreateSolidBrush(color);
    FillRect(dc, &rect, brush);
    DeleteObject(brush);
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

int selected_index(const ShUi* ui) {
    return static_cast<int>(SendMessageW(ui->list, LB_GETCURSEL, 0, 0));
}

void repopulate(ShUi* ui) {
    SendMessageW(ui->list, LB_RESETCONTENT, 0, 0);
    for (const auto& site : ui->config.sites) {
        std::string row = site.first + "  —  shields down";
        std::wstring wide(row.begin(), row.end());
        SendMessageW(ui->list, LB_ADDSTRING, 0,
            reinterpret_cast<LPARAM>(wide.c_str()));
    }
}

void persist(ShUi* ui) {
    Runtime* rt = runtime();
    if (!rt) return;
    shields::save_shields(rt->config_dir, ui->config);
    rt->shields_cfg = ui->config;
}

void add_site(ShUi* ui) {
    Runtime* rt = runtime();
    if (!rt) return;
    std::string host = shields::normalize_host(edit_text(ui->host));
    if (host.empty() || !shields::valid_domain(host)) return;
    shields::shields_set_site_mode(ui->config, host, "down");
    persist(ui);
    repopulate(ui);
    set_edit_utf8(ui->host, "");
}

void remove_selected(ShUi* ui) {
    const int index = selected_index(ui);
    if (index < 0 || index >= static_cast<int>(ui->config.sites.size())) return;
    ui->config.sites.erase(ui->config.sites.begin() + index);
    persist(ui);
    repopulate(ui);
    const int next = index < static_cast<int>(ui->config.sites.size())
        ? index : static_cast<int>(ui->config.sites.size()) - 1;
    SendMessageW(ui->list, LB_SETCURSEL, next, 0);
}

void clear_sites(ShUi* ui) {
    if (ui->config.sites.empty()) return;
    ui->config.sites.clear();
    persist(ui);
    repopulate(ui);
}

void layout_dialog(ShUi* ui) {
    if (!ui->hwnd) return;
    RECT client;
    GetClientRect(ui->hwnd, &client);
    const int dpi = dpi_for(ui->hwnd);
    const int margin = MulDiv(14, dpi, 96);
    const int w = client.right - margin * 2;
    const int list_bottom = client.bottom - MulDiv(96, dpi, 96);
    MoveWindow(ui->list, margin, margin, w, list_bottom - margin * 2, TRUE);
    const int field_y = list_bottom + MulDiv(4, dpi, 96);
    const int row_h = MulDiv(24, dpi, 96);
    MoveWindow(ui->host, margin, field_y, w, row_h, TRUE);
    MoveWindow(GetDlgItem(ui->hwnd, ID_HINT), margin, field_y + row_h + 2,
               w, row_h, TRUE);
    const int btn_y = client.bottom - MulDiv(34, dpi, 96);
    const int btn_w = MulDiv(96, dpi, 96);
    MoveWindow(GetDlgItem(ui->hwnd, ID_ADD), margin, btn_y, btn_w,
               MulDiv(26, dpi, 96), TRUE);
    MoveWindow(GetDlgItem(ui->hwnd, ID_REMOVE),
               margin + btn_w + MulDiv(8, dpi, 96), btn_y, btn_w,
               MulDiv(26, dpi, 96), TRUE);
    MoveWindow(GetDlgItem(ui->hwnd, ID_CLEAR),
               margin + (btn_w + MulDiv(8, dpi, 96)) * 2, btn_y, btn_w,
               MulDiv(26, dpi, 96), TRUE);
    MoveWindow(GetDlgItem(ui->hwnd, ID_CLOSE),
               client.right - margin - btn_w, btn_y, btn_w,
               MulDiv(26, dpi, 96), TRUE);
}

void paint_list_item(ShUi* ui, const DRAWITEMSTRUCT* draw) {
    const gfx::AuxTheme& t = gfx::aux_theme();
    const int index = static_cast<int>(draw->itemID);
    const bool selected = (draw->itemState & ODS_SELECTED) != 0;
    fill_rect_color(draw->hDC, draw->rcItem,
                    selected ? t.field_bg : t.surface);
    if (selected) {
        RECT focus = draw->rcItem;
        HBRUSH ring = CreateSolidBrush(gfx::palette().accent);
        FrameRect(draw->hDC, &focus, ring);
        DeleteObject(ring);
    }
    if (index < 0 || index >= static_cast<int>(ui->config.sites.size())) return;
    int dpi = dpi_for(ui->hwnd);
    RECT glyph_box = {draw->rcItem.left + MulDiv(10, dpi, 96),
                      (draw->rcItem.top + draw->rcItem.bottom) / 2
                          - MulDiv(8, dpi, 96),
                      draw->rcItem.left + MulDiv(26, dpi, 96),
                      (draw->rcItem.top + draw->rcItem.bottom) / 2
                          + MulDiv(8, dpi, 96)};
    gfx::draw_glyph(draw->hDC, glyph_box, gfx::Glyph::Shield,
                    selected ? gfx::palette().accent : t.muted, 1.4f, dpi);
    const std::string& host = ui->config.sites[static_cast<size_t>(index)].first;
    RECT text = {glyph_box.right + MulDiv(6, dpi, 96), draw->rcItem.top,
                 draw->rcItem.right, draw->rcItem.bottom};
    SetBkMode(draw->hDC, TRANSPARENT);
    SelectObject(draw->hDC, ui->font);
    SetTextColor(draw->hDC, selected ? t.ink : t.muted);
    std::wstring wide(host.begin(), host.end());
    DrawTextW(draw->hDC, wide.c_str(), -1, &text,
              DT_END_ELLIPSIS | DT_SINGLELINE | DT_VCENTER | DT_LEFT);
}

LRESULT CALLBACK wnd_proc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
    ShUi* ui = state_for(hwnd);
    switch (msg) {
        case WM_CREATE: {
            auto* create = reinterpret_cast<CREATESTRUCTW*>(lparam);
            SetWindowLongPtrW(hwnd, GWLP_USERDATA,
                reinterpret_cast<LONG_PTR>(create->lpCreateParams));
            return 0;
        }
        case WM_DESTROY: {
            if (ui) {
                DeleteObject(ui->font);
                delete ui;
                SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
            }
            return 0;
        }
        case WM_ERASEBKGND:
            return aux_erasebkgnd(hwnd, wparam);
        case WM_SIZE:
            if (ui) layout_dialog(ui);
            return 0;
        case WM_DRAWITEM: {
            auto* draw = reinterpret_cast<DRAWITEMSTRUCT*>(lparam);
            if (draw->CtlID == ID_LIST && ui) {
                paint_list_item(ui, draw);
                return TRUE;
            }
            break;
        }
        case WM_MEASUREITEM: {
            auto* measure = reinterpret_cast<MEASUREITEMSTRUCT*>(lparam);
            if (measure->CtlID == ID_LIST) {
                measure->itemHeight = MulDiv(28, dpi_for(hwnd), 96);
                return TRUE;
            }
            break;
        }
        case WM_CTLCOLORSTATIC:
            return aux_static_ctlcolor(reinterpret_cast<HDC>(wparam));
        case WM_CTLCOLOREDIT:
        case WM_CTLCOLORLISTBOX:
            return aux_field_ctlcolor(reinterpret_cast<HDC>(wparam));
        case WM_COMMAND: {
            if (!ui) break;
            const int id = LOWORD(wparam);
            const int code = HIWORD(wparam);
            switch (id) {
                case ID_HOST:
                    if (code == EN_KILLFOCUS) add_site(ui);
                    return 0;
                case ID_ADD:
                    add_site(ui);
                    return 0;
                case ID_REMOVE:
                    remove_selected(ui);
                    return 0;
                case ID_CLEAR:
                    clear_sites(ui);
                    return 0;
                case ID_CLOSE:
                    DestroyWindow(hwnd);
                    return 0;
                default:
                    return 0;
            }
        }
        default:
            break;
    }
    return DefWindowProcW(hwnd, msg, wparam, lparam);
}

HWND make_control(HWND parent, const wchar_t* cls, const wchar_t* text, DWORD style,
                  int id, int x, int y, int w, int h, HFONT font) {
    HWND ctrl = CreateWindowExW(
        wcscmp(cls, L"EDIT") == 0 ? WS_EX_CLIENTEDGE : 0, cls, text,
        WS_CHILD | style, x, y, w, h, parent,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
        GetModuleHandleW(nullptr), nullptr);
    SendMessageW(ctrl, WM_SETFONT, (WPARAM)font, TRUE);
    apply_control_theme(ctrl);
    return ctrl;
}

}  // namespace

void open_shields_window(HWND owner) {
    static bool registered = false;
    if (!registered) {
        WNDCLASSW wc = {};
        wc.lpfnWndProc = wnd_proc;
        wc.hInstance = GetModuleHandleW(nullptr);
        wc.hCursor = LoadCursorW(nullptr, reinterpret_cast<LPCWSTR>(IDC_ARROW));
        wc.hbrBackground = nullptr;
        wc.lpszClassName = kClass;
        RegisterClassW(&wc);
        registered = true;
    }

    Runtime* rt = runtime();
    if (!rt) return;

    HWND existing = FindWindowExW(nullptr, nullptr, kClass, nullptr);
    if (existing) {
        ShowWindow(existing, SW_RESTORE);
        SetForegroundWindow(existing);
        return;
    }

    auto* ui = new ShUi();
    ui->config = shields::load_shields(rt->config_dir);

    RECT area = {0, 0, 440, 400};
    AdjustWindowRect(&area, WS_OVERLAPPEDWINDOW, FALSE);
    ui->hwnd = CreateWindowExW(0, kClass, L"Kiwi shield exceptions",
        WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT,
        area.right - area.left, area.bottom - area.top,
        owner, nullptr, GetModuleHandleW(nullptr), ui);
    if (!ui->hwnd) {
        delete ui;
        return;
    }

    const int dpi = dpi_for(ui->hwnd);
    ui->font = CreateFontW(-MulDiv(10, dpi, 72), 0, 0, 0, FW_NORMAL, FALSE, FALSE,
        FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");

    ui->list = CreateWindowExW(WS_EX_CLIENTEDGE, L"LISTBOX", nullptr,
        WS_CHILD | WS_VISIBLE | WS_VSCROLL | LBS_NOTIFY | LBS_OWNERDRAWVARIABLE
            | LBS_HASSTRINGS,
        0, 0, 10, 10, ui->hwnd,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(ID_LIST)),
        GetModuleHandleW(nullptr), nullptr);
    SendMessageW(ui->list, WM_SETFONT, (WPARAM)ui->font, TRUE);
    apply_list_theme(ui->list);

    ui->host = make_control(ui->hwnd, L"EDIT", nullptr,
                            WS_VISIBLE | ES_AUTOHSCROLL, ID_HOST,
                            0, 0, 10, 10, ui->font);
    SendMessageW(ui->host, EM_SETCUEBANNER, TRUE,
        reinterpret_cast<LPARAM>(L"example.com — press Add to lower shields"));
    make_control(ui->hwnd, L"STATIC",
                 L"Sites listed here browse with shields down.",
                 WS_VISIBLE, ID_HINT, 0, 0, 10, 10, ui->font);
    make_control(ui->hwnd, L"BUTTON", L"Add",
                 WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON, ID_ADD,
                 0, 0, 10, 10, ui->font);
    make_control(ui->hwnd, L"BUTTON", L"Remove",
                 WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON, ID_REMOVE,
                 0, 0, 10, 10, ui->font);
    make_control(ui->hwnd, L"BUTTON", L"Clear all",
                 WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON, ID_CLEAR,
                 0, 0, 10, 10, ui->font);
    make_control(ui->hwnd, L"BUTTON", L"Close",
                 WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON, ID_CLOSE,
                 0, 0, 10, 10, ui->font);

    repopulate(ui);
    layout_dialog(ui);
    ShowWindow(ui->hwnd, SW_SHOW);
    UpdateWindow(ui->hwnd);
}

}

