// Kiwi workspaces manager — a small aux dialog over the shared
// workspaces.toml store. Layout follows the search-engines editor: a list
// on the left, inline edit fields and buttons on the right, themed through
// aux_theme_win so every dialog speaks the same visual language.

#include "workspaces_win.hpp"

#include "../cef_runtime.hpp"
#include "../../workspaces_store.hpp"
#include "aux_theme_win.hpp"
#include "cef_shell_win.hpp"
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

#include <cstring>
#include <string>
#include <vector>

namespace kiwi {

namespace {

constexpr wchar_t kClass[] = L"KiwiWorkspacesWindow";

constexpr int ID_LIST = 101;
constexpr int ID_NAME_LABEL = 102;
constexpr int ID_NAME = 103;
constexpr int ID_GLYPH_LABEL = 104;
constexpr int ID_GLYPH = 105;
constexpr int ID_ADD = 106;
constexpr int ID_REMOVE = 107;
constexpr int ID_CLOSE = 108;

struct WsUi {
    HWND hwnd = nullptr;
    HWND list = nullptr;
    HWND name = nullptr;
    HWND glyph = nullptr;
    std::vector<WorkspaceDef> defs;
    std::string original;  // keyword of the entry loaded into the editors
    HFONT font = nullptr;
    HFONT bold = nullptr;
};

WsUi* state_for(HWND hwnd) {
    return reinterpret_cast<WsUi*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
}

void fill_rect_color(HDC dc, const RECT& rect, COLORREF color) {
    HBRUSH brush = CreateSolidBrush(color);
    FillRect(dc, &rect, brush);
    DeleteObject(brush);
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

int selected_index(const WsUi* ui) {
    return static_cast<int>(SendMessageW(ui->list, LB_GETCURSEL, 0, 0));
}

void repopulate(WsUi* ui) {
    SendMessageW(ui->list, LB_RESETCONTENT, 0, 0);
    for (const WorkspaceDef& def : ui->defs) {
        std::wstring row = L"        " + std::wstring(def.name.begin(), def.name.end());
        int index = static_cast<int>(SendMessageW(ui->list, LB_ADDSTRING, 0,
            reinterpret_cast<LPARAM>(row.c_str())));
        SendMessageW(ui->list, LB_SETITEMDATA, index, def.id);
    }
}

// Pushes the editors' content into the selected entry and persists.
void commit_editors(WsUi* ui) {
    Runtime* rt = runtime();
    if (!rt) return;
    const int index = selected_index(ui);
    if (index < 0 || index >= static_cast<int>(ui->defs.size())) return;
    WorkspaceDef& def = ui->defs[static_cast<size_t>(index)];
    std::string name = edit_text(ui->name);
    if (!name.empty() && name != def.name) def.name = name.substr(0, 32);
    int glyph = static_cast<int>(SendMessageW(ui->glyph, CB_GETCURSEL, 0, 0));
    if (glyph >= 0 && glyph < workspace_glyph_count()) {
        def.glyph = kWorkspaceGlyphs[glyph];
    }
    save_workspaces(rt->config_dir, ui->defs);
    sync_workspaces();
    repopulate(ui);
    SendMessageW(ui->list, LB_SETCURSEL, index, 0);
}

void load_selected(WsUi* ui) {
    const int index = selected_index(ui);
    if (index < 0 || index >= static_cast<int>(ui->defs.size())) {
        set_edit_utf8(ui->name, "");
        return;
    }
    const WorkspaceDef& def = ui->defs[static_cast<size_t>(index)];
    set_edit_utf8(ui->name, def.name);
    for (int i = 0; i < workspace_glyph_count(); ++i) {
        if (def.glyph == kWorkspaceGlyphs[i]) {
            SendMessageW(ui->glyph, CB_SETCURSEL, i, 0);
            break;
        }
    }
}

void add_workspace(WsUi* ui) {
    Runtime* rt = runtime();
    if (!rt) return;
    if (static_cast<int>(ui->defs.size()) >= kMaxWorkspaces) return;
    const int id = next_workspace_id(ui->defs);
    if (id <= 0) return;
    std::string name = "Workspace " + std::to_string(ui->defs.size() + 1);
    // Cycle through the glyph table so every new space looks distinct.
    const std::string glyph = kWorkspaceGlyphs[ui->defs.size() % workspace_glyph_count()];
    ui->defs.push_back(WorkspaceDef{id, name, glyph});
    save_workspaces(rt->config_dir, ui->defs);
    sync_workspaces();
    repopulate(ui);
    SendMessageW(ui->list, LB_SETCURSEL,
                 static_cast<int>(ui->defs.size()) - 1, 0);
    load_selected(ui);
}

void remove_selected(WsUi* ui) {
    Runtime* rt = runtime();
    if (!rt) return;
    const int index = selected_index(ui);
    if (index < 0 || index >= static_cast<int>(ui->defs.size())) return;
    if (ui->defs.size() <= 1) return;  // the last workspace stays
    ui->defs.erase(ui->defs.begin() + index);
    save_workspaces(rt->config_dir, ui->defs);
    sync_workspaces();
    repopulate(ui);
    const int next = index < static_cast<int>(ui->defs.size()) ? index
                   : static_cast<int>(ui->defs.size()) - 1;
    SendMessageW(ui->list, LB_SETCURSEL, next, 0);
    load_selected(ui);
}

void layout_dialog(WsUi* ui) {
    if (!ui->hwnd) return;
    RECT client;
    GetClientRect(ui->hwnd, &client);
    const int dpi = dpi_for(ui->hwnd);
    const int margin = MulDiv(14, dpi, 96);
    const int w = client.right - margin * 2;
    const int list_bottom = client.bottom - MulDiv(56, dpi, 96);
    MoveWindow(ui->list, margin, margin, w, list_bottom - margin * 2, TRUE);
    const int col_w = MulDiv(64, dpi, 96);
    const int field_y = list_bottom + MulDiv(4, dpi, 96);
    const int row_h = MulDiv(24, dpi, 96);
    MoveWindow(GetDlgItem(ui->hwnd, ID_NAME_LABEL), margin, field_y + 3,
               col_w, row_h, TRUE);
    MoveWindow(ui->name, margin + col_w, field_y, MulDiv(180, dpi, 96), row_h, TRUE);
    MoveWindow(GetDlgItem(ui->hwnd, ID_GLYPH_LABEL),
               margin + col_w + MulDiv(196, dpi, 96), field_y + 3,
               col_w, row_h, TRUE);
    MoveWindow(ui->glyph,
               margin + col_w * 2 + MulDiv(196, dpi, 96), field_y,
               MulDiv(130, dpi, 96), row_h + MulDiv(90, dpi, 96), TRUE);
    const int btn_y = client.bottom - MulDiv(34, dpi, 96);
    const int btn_w = MulDiv(96, dpi, 96);
    MoveWindow(GetDlgItem(ui->hwnd, ID_ADD), margin, btn_y, btn_w,
               MulDiv(26, dpi, 96), TRUE);
    MoveWindow(GetDlgItem(ui->hwnd, ID_REMOVE),
               margin + btn_w + MulDiv(8, dpi, 96), btn_y, btn_w,
               MulDiv(26, dpi, 96), TRUE);
    MoveWindow(GetDlgItem(ui->hwnd, ID_CLOSE),
               client.right - margin - btn_w, btn_y, btn_w,
               MulDiv(26, dpi, 96), TRUE);
}

// Owner-draw: paints the workspace glyph before each list row.
void paint_list_item(const WsUi* ui, const DRAWITEMSTRUCT* draw) {
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
    if (index < 0 || index >= static_cast<int>(ui->defs.size())) return;
    const WorkspaceDef& def = ui->defs[static_cast<size_t>(index)];
    int dpi = dpi_for(ui->hwnd);
    RECT glyph_box = {draw->rcItem.left + MulDiv(10, dpi, 96),
                      (draw->rcItem.top + draw->rcItem.bottom) / 2
                          - MulDiv(8, dpi, 96),
                      draw->rcItem.left + MulDiv(26, dpi, 96),
                      (draw->rcItem.top + draw->rcItem.bottom) / 2
                          + MulDiv(8, dpi, 96)};
    gfx::Glyph glyph = gfx::Glyph::Globe;
    gfx::glyph_from_name(def.glyph, &glyph);
    gfx::draw_glyph(draw->hDC, glyph_box, glyph,
                    selected ? gfx::palette().accent : t.ink, 1.4f, dpi);
    RECT text = {glyph_box.right + MulDiv(6, dpi, 96), draw->rcItem.top,
                 draw->rcItem.right, draw->rcItem.bottom};
    SetBkMode(draw->hDC, TRANSPARENT);
    SelectObject(draw->hDC, ui->font);
    SetTextColor(draw->hDC, selected ? t.ink : t.muted);
    std::wstring name(def.name.begin(), def.name.end());
    DrawTextW(draw->hDC, name.c_str(), -1, &text,
              DT_END_ELLIPSIS | DT_SINGLELINE | DT_VCENTER | DT_LEFT);
}

LRESULT CALLBACK wnd_proc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
    WsUi* ui = state_for(hwnd);
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
                DeleteObject(ui->bold);
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
                case ID_LIST:
                    if (code == LBN_SELCHANGE) load_selected(ui);
                    if (code == LBN_DBLCLK) commit_editors(ui);
                    return 0;
                case ID_NAME:
                    if (code == EN_KILLFOCUS) commit_editors(ui);
                    return 0;
                case ID_GLYPH:
                    if (code == CBN_SELCHANGE) commit_editors(ui);
                    return 0;
                case ID_ADD:
                    add_workspace(ui);
                    return 0;
                case ID_REMOVE:
                    remove_selected(ui);
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

void open_workspaces_window(HWND owner) {
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

    auto* ui = new WsUi();
    ui->defs = load_workspaces(rt->config_dir);

    RECT area = {0, 0, 480, 420};
    AdjustWindowRect(&area, WS_OVERLAPPEDWINDOW, FALSE);
    ui->hwnd = CreateWindowExW(0, kClass, L"Kiwi workspaces",
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
    ui->bold = CreateFontW(-MulDiv(10, dpi, 72), 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE,
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

    make_control(ui->hwnd, L"STATIC", L"Name", WS_VISIBLE, ID_NAME_LABEL,
                 0, 0, 10, 10, ui->font);
    ui->name = make_control(ui->hwnd, L"EDIT", nullptr,
                            WS_VISIBLE | ES_AUTOHSCROLL, ID_NAME,
                            0, 0, 10, 10, ui->font);
    make_control(ui->hwnd, L"STATIC", L"Glyph", WS_VISIBLE, ID_GLYPH_LABEL,
                 0, 0, 10, 10, ui->font);
    ui->glyph = make_control(ui->hwnd, L"COMBOBOX", nullptr,
                             WS_VISIBLE | CBS_DROPDOWNLIST, ID_GLYPH,
                             0, 0, 10, 10, ui->font);
    for (int i = 0; i < workspace_glyph_count(); ++i) {
        std::wstring item(kWorkspaceGlyphs[i], kWorkspaceGlyphs[i]
            + strlen(kWorkspaceGlyphs[i]));
        SendMessageW(ui->glyph, CB_ADDSTRING, 0,
            reinterpret_cast<LPARAM>(item.c_str()));
    }
    make_control(ui->hwnd, L"BUTTON", L"Add workspace",
                 WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON, ID_ADD,
                 0, 0, 10, 10, ui->font);
    make_control(ui->hwnd, L"BUTTON", L"Remove",
                 WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON, ID_REMOVE,
                 0, 0, 10, 10, ui->font);
    make_control(ui->hwnd, L"BUTTON", L"Close",
                 WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON, ID_CLOSE,
                 0, 0, 10, 10, ui->font);

    repopulate(ui);
    SendMessageW(ui->list, LB_SETCURSEL, 0, 0);
    load_selected(ui);
    layout_dialog(ui);
    ShowWindow(ui->hwnd, SW_SHOW);
    UpdateWindow(ui->hwnd);
}

}

