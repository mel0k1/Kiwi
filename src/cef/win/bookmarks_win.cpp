#include "bookmarks_win.hpp"

#include "../cef_runtime.hpp"
#include "aux_theme_win.hpp"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <commctrl.h>

#include <ctime>
#include <functional>
#include <string>
#include <vector>

namespace kiwi {

namespace {

constexpr wchar_t kClass[] = L"KiwiBookmarksWindow";

enum {
    ID_LIST = 2001,
    ID_URL,
    ID_TITLE,
    ID_OPEN,
    ID_SAVE,
    ID_REMOVE,
    ID_CLOSEWIN,
};

struct ManagerState {
    HWND hwnd = nullptr;
    HWND list = nullptr;
    HWND url = nullptr;
    HWND title = nullptr;
    std::function<void(const std::string&)> open_url;
    std::vector<mustela::BookmarkEntry> snapshot;
};

ManagerState* state_for(HWND hwnd) {
    return reinterpret_cast<ManagerState*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
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

std::wstring to_wide(const std::string& s) {
    if (s.empty()) return std::wstring();
    int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), static_cast<int>(s.size()),
        nullptr, 0);
    std::wstring w(static_cast<size_t>(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), static_cast<int>(s.size()), w.data(), n);
    return w;
}

void reload_list(ManagerState* st) {
    Runtime* rt = runtime();
    if (!rt || !rt->bookmarks) return;
    ListView_DeleteAllItems(st->list);
    st->snapshot = rt->bookmarks->entries();
    for (size_t i = 0; i < st->snapshot.size(); ++i) {
        const auto& e = st->snapshot[i];
        std::wstring title = to_wide(e.title);
        std::wstring url = to_wide(e.url);
        LVITEMW item = {};
        item.mask = LVIF_TEXT | LVIF_PARAM;
        item.iItem = static_cast<int>(i);
        item.pszText = title.empty() ? const_cast<LPWSTR>(L"(untitled)")
                                     : title.data();
        item.lParam = static_cast<LPARAM>(i);
        int index = ListView_InsertItem(st->list, &item);
        ListView_SetItemText(st->list, index, 1, url.empty()
            ? const_cast<LPWSTR>(L"") : url.data());
    }
}

void sync_edits(ManagerState* st) {
    int sel = ListView_GetNextItem(st->list, -1, LVNI_SELECTED);
    if (sel < 0 || sel >= static_cast<int>(st->snapshot.size())) {
        SetWindowTextW(st->url, L"");
        SetWindowTextW(st->title, L"");
        return;
    }
    set_edit_utf8(st->url, st->snapshot[static_cast<size_t>(sel)].url);
    set_edit_utf8(st->title, st->snapshot[static_cast<size_t>(sel)].title);
}

void layout(ManagerState* st) {
    RECT rc;
    GetClientRect(st->hwnd, &rc);
    int w = rc.right - rc.left;
    int h = rc.bottom - rc.top;
    int pad = 8;
    int footer = 104;
    MoveWindow(st->list, pad, pad, w - pad * 2, h - footer - pad, TRUE);
    int y = h - footer;
    MoveWindow(st->title, pad, y, w - pad * 2, 24, TRUE);
    MoveWindow(st->url, pad, y + 28, w - pad * 2, 24, TRUE);
    int bw = 110;
    int gap = 6;
    int bx = pad;
    for (int id : {ID_OPEN, ID_SAVE, ID_REMOVE, ID_CLOSEWIN}) {
        MoveWindow(GetDlgItem(st->hwnd, id), bx, y + 62, bw, 26, TRUE);
        bx += bw + gap;
    }
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
            ManagerState* st = state_for(hwnd);
            if (st) {
                st->hwnd = nullptr;
                delete st;
                SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
            }
            return 0;
        }
        default:
            break;
    }
    ManagerState* st = state_for(hwnd);
    if (!st) return DefWindowProcW(hwnd, msg, wparam, lparam);

    switch (msg) {
        case WM_ERASEBKGND:
            return aux_erasebkgnd(hwnd, wparam);
        case WM_CTLCOLOREDIT:
            return aux_field_ctlcolor(reinterpret_cast<HDC>(wparam));
        case WM_CTLCOLORSTATIC:
            return aux_static_ctlcolor(reinterpret_cast<HDC>(wparam));
        case WM_NOTIFY: {
            auto* nm = reinterpret_cast<NMHDR*>(lparam);
            if (nm->idFrom == ID_LIST && nm->code == LVN_ITEMCHANGED) {
                sync_edits(st);
            }
            if (nm->idFrom == ID_LIST && nm->code == NM_DBLCLK) {
                auto* item = reinterpret_cast<NMITEMACTIVATE*>(lparam);
                if (item->iItem >= 0
                    && item->iItem < static_cast<int>(st->snapshot.size())
                    && st->open_url) {
                    st->open_url(st->snapshot[static_cast<size_t>(item->iItem)].url);
                }
            }
            return 0;
        }
        case WM_COMMAND: {
            int id = LOWORD(wparam);
            Runtime* rt = runtime();
            if (!rt || !rt->bookmarks) return 0;
            int sel = ListView_GetNextItem(st->list, -1, LVNI_SELECTED);
            if (id == ID_OPEN) {
                if (sel >= 0 && sel < static_cast<int>(st->snapshot.size())
                    && st->open_url) {
                    st->open_url(st->snapshot[static_cast<size_t>(sel)].url);
                }
                return 0;
            }
            if (id == ID_SAVE) {
                std::string url = edit_text(st->url);
                std::string title = edit_text(st->title);
                if (url.empty()) return 0;
                if (sel >= 0 && sel < static_cast<int>(st->snapshot.size())) {
                    const std::string& old_url =
                        st->snapshot[static_cast<size_t>(sel)].url;
                    if (old_url != url) rt->bookmarks->remove(old_url);
                }
                rt->bookmarks->add(url, title, std::time(nullptr));
                reload_list(st);
                return 0;
            }
            if (id == ID_REMOVE) {
                if (sel >= 0 && sel < static_cast<int>(st->snapshot.size())) {
                    rt->bookmarks->remove(st->snapshot[static_cast<size_t>(sel)].url);
                    SetWindowTextW(st->url, L"");
                    SetWindowTextW(st->title, L"");
                    reload_list(st);
                }
                return 0;
            }
            if (id == ID_CLOSEWIN) DestroyWindow(hwnd);
            return 0;
        }
        case WM_SIZE:
            layout(st);
            return 0;
        default:
            break;
    }
    return DefWindowProcW(hwnd, msg, wparam, lparam);
}

}  // namespace

void open_bookmarks_manager(HWND owner,
                            std::function<void(const std::string&)> open_url) {
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
    if (!rt || !rt->bookmarks) return;

    // Only one manager at a time: owned windows survive FindWindow round trips.
    HWND existing = FindWindowExW(owner, nullptr, kClass, nullptr);
    if (existing) {
        ShowWindow(existing, SW_RESTORE);
        SetForegroundWindow(existing);
        return;
    }

    auto* st = new ManagerState();
    st->open_url = std::move(open_url);
    RECT area = {0, 0, 560, 460};
    AdjustWindowRect(&area, WS_OVERLAPPEDWINDOW, FALSE);
    st->hwnd = CreateWindowExW(0, kClass, L"Kiwi bookmarks",
        WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT,
        area.right - area.left, area.bottom - area.top,
        owner, nullptr, GetModuleHandleW(nullptr), st);
    if (!st->hwnd) {
        delete st;
        return;
    }

    st->list = CreateWindowExW(0, WC_LISTVIEWW, nullptr,
        WS_CHILD | WS_VISIBLE | WS_BORDER | LVS_REPORT | LVS_SHOWSELALWAYS
            | LVS_SINGLESEL,
        0, 0, 10, 10, st->hwnd,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(ID_LIST)),
        GetModuleHandleW(nullptr), nullptr);
    ListView_SetExtendedListViewStyle(st->list,
        LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER);
    apply_list_theme(st->list);
    LVCOLUMNW col = {};
    col.mask = LVCF_TEXT | LVCF_WIDTH;
    col.cx = 220;
    col.pszText = const_cast<LPWSTR>(L"Title");
    ListView_InsertColumn(st->list, 0, &col);
    col.cx = 300;
    col.pszText = const_cast<LPWSTR>(L"URL");
    ListView_InsertColumn(st->list, 1, &col);

    st->title = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", nullptr,
        WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL, 0, 0, 10, 10, st->hwnd,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(ID_TITLE)),
        GetModuleHandleW(nullptr), nullptr);
    st->url = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", nullptr,
        WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL, 0, 0, 10, 10, st->hwnd,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(ID_URL)),
        GetModuleHandleW(nullptr), nullptr);

    struct Button { int id; const wchar_t* label; };
    const Button buttons[] = {
        {ID_OPEN, L"Open"}, {ID_SAVE, L"Save"}, {ID_REMOVE, L"Remove"},
        {ID_CLOSEWIN, L"Close"},
    };
    for (const auto& b : buttons) {
        CreateWindowExW(0, L"BUTTON", b.label, WS_CHILD | WS_VISIBLE | WS_TABSTOP,
            0, 0, 10, 10, st->hwnd,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(b.id)),
            GetModuleHandleW(nullptr), nullptr);
    }

    HFONT font = CreateFontW(-MulDiv(10, 96, 72), 0, 0, 0, FW_NORMAL, FALSE, FALSE,
        FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
    SendMessageW(st->list, WM_SETFONT, (WPARAM)font, TRUE);
    for (int id : {ID_TITLE, ID_URL, ID_OPEN, ID_SAVE, ID_REMOVE, ID_CLOSEWIN}) {
        SendMessageW(GetDlgItem(st->hwnd, id), WM_SETFONT, (WPARAM)font, TRUE);
        apply_control_theme(GetDlgItem(st->hwnd, id));
    }

    reload_list(st);
    ShowWindow(st->hwnd, SW_SHOW);
    UpdateWindow(st->hwnd);
    layout(st);
}

}  // namespace kiwi
