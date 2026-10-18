#include "history_win.hpp"

#include "../cef_runtime.hpp"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <commctrl.h>

#include <ctime>
#include <string>
#include <vector>

namespace kiwi {

namespace {

constexpr wchar_t kClass[] = L"KiwiHistoryWindow";
constexpr COLORREF kSurface = RGB(255, 245, 240);

enum {
    ID_LIST = 5001,
    ID_OPEN,
    ID_CLEAR,
    ID_CLOSEWIN,
};

struct HistoryState {
    HWND hwnd = nullptr;
    HWND list = nullptr;
    std::vector<mustela::HistoryEntry> snapshot;
    std::function<void(const std::string&)> open_url;
};

HistoryState* state_for(HWND hwnd) {
    return reinterpret_cast<HistoryState*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
}

std::wstring to_wide(const std::string& s) {
    if (s.empty()) return std::wstring();
    int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), static_cast<int>(s.size()),
        nullptr, 0);
    std::wstring w(static_cast<size_t>(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), static_cast<int>(s.size()), w.data(), n);
    return w;
}

void reload_list(HistoryState* st) {
    Runtime* rt = runtime();
    if (!rt || !rt->history) return;
    ListView_DeleteAllItems(st->list);
    st->snapshot = rt->history->recent(1000);
    wchar_t stamp[32] = {};
    for (size_t i = 0; i < st->snapshot.size(); ++i) {
        const auto& e = st->snapshot[i];
        std::wstring title = to_wide(e.title);
        std::wstring url = to_wide(e.url);
        std::tm tm_now{};
        time_t when = static_cast<time_t>(e.visited_at);
        localtime_s(&tm_now, &when);
        std::wcsftime(stamp, 32, L"%Y-%m-%d %H:%M", &tm_now);
        LVITEMW item = {};
        item.mask = LVIF_TEXT | LVIF_PARAM;
        item.iItem = static_cast<int>(i);
        item.pszText = stamp;
        item.lParam = static_cast<LPARAM>(i);
        int index = ListView_InsertItem(st->list, &item);
        ListView_SetItemText(st->list, index, 1, title.empty()
            ? const_cast<LPWSTR>(L"(untitled)") : title.data());
        ListView_SetItemText(st->list, index, 2, url.empty()
            ? const_cast<LPWSTR>(L"") : url.data());
    }
}

void layout(HistoryState* st) {
    RECT rc;
    GetClientRect(st->hwnd, &rc);
    int w = rc.right - rc.left;
    int h = rc.bottom - rc.top;
    int pad = 8;
    int footer = 44;
    MoveWindow(st->list, pad, pad, w - pad * 2, h - footer - pad, TRUE);
    int bw = 130;
    int bx = pad;
    for (int id : {ID_OPEN, ID_CLEAR, ID_CLOSEWIN}) {
        MoveWindow(GetDlgItem(st->hwnd, id), bx, h - footer + 6, bw, 26, TRUE);
        bx += bw + 6;
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
            HistoryState* st = state_for(hwnd);
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
    HistoryState* st = state_for(hwnd);
    if (!st) return DefWindowProcW(hwnd, msg, wparam, lparam);

    switch (msg) {
        case WM_NOTIFY: {
            auto* nm = reinterpret_cast<NMHDR*>(lparam);
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
            if (id == ID_OPEN) {
                int sel = ListView_GetNextItem(st->list, -1, LVNI_SELECTED);
                if (sel >= 0 && sel < static_cast<int>(st->snapshot.size())
                    && st->open_url) {
                    st->open_url(st->snapshot[static_cast<size_t>(sel)].url);
                }
                return 0;
            }
            if (id == ID_CLEAR) {
                Runtime* rt = runtime();
                if (rt && rt->history) {
                    rt->history->clear();
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

void open_history_window(HWND owner,
                         std::function<void(const std::string&)> open_url) {
    static bool registered = false;
    if (!registered) {
        WNDCLASSW wc = {};
        wc.lpfnWndProc = wnd_proc;
        wc.hInstance = GetModuleHandleW(nullptr);
        wc.hCursor = LoadCursorW(nullptr, reinterpret_cast<LPCWSTR>(IDC_ARROW));
        wc.hbrBackground = CreateSolidBrush(kSurface);
        wc.lpszClassName = kClass;
        RegisterClassW(&wc);
        registered = true;
    }

    Runtime* rt = runtime();
    if (!rt || !rt->history) return;

    HWND existing = FindWindowExW(owner, nullptr, kClass, nullptr);
    if (existing) {
        ShowWindow(existing, SW_RESTORE);
        SetForegroundWindow(existing);
        return;
    }

    auto* st = new HistoryState();
    st->open_url = std::move(open_url);
    RECT area = {0, 0, 640, 460};
    AdjustWindowRect(&area, WS_OVERLAPPEDWINDOW, FALSE);
    st->hwnd = CreateWindowExW(0, kClass, L"Kiwi history",
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
    LVCOLUMNW col = {};
    col.mask = LVCF_TEXT | LVCF_WIDTH;
    col.cx = 130;
    col.pszText = const_cast<LPWSTR>(L"Visited");
    ListView_InsertColumn(st->list, 0, &col);
    col.cx = 200;
    col.pszText = const_cast<LPWSTR>(L"Title");
    ListView_InsertColumn(st->list, 1, &col);
    col.cx = 260;
    col.pszText = const_cast<LPWSTR>(L"URL");
    ListView_InsertColumn(st->list, 2, &col);

    struct Button { int id; const wchar_t* label; };
    const Button buttons[] = {
        {ID_OPEN, L"Open"}, {ID_CLEAR, L"Clear history"}, {ID_CLOSEWIN, L"Close"},
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
    for (int id : {ID_OPEN, ID_CLEAR, ID_CLOSEWIN}) {
        SendMessageW(GetDlgItem(st->hwnd, id), WM_SETFONT, (WPARAM)font, TRUE);
    }

    reload_list(st);
    ShowWindow(st->hwnd, SW_SHOW);
    UpdateWindow(st->hwnd);
    layout(st);
}

}  // namespace kiwi
