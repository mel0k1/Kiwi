#include "clear_data_win.hpp"

#include "../cef_runtime.hpp"
#include "aux_theme_win.hpp"
#include "../cef_client.hpp"

#include "include/cef_cookie.h"
#include "include/cef_request_context.h"

#include <commctrl.h>

#include <functional>
#include <string>

namespace kiwi {

namespace {

constexpr wchar_t kDialogClass[] = L"KiwiClearData";

enum DialogControl {
    IDC_HISTORY_CHECK = 3501,
    IDC_COOKIES_CHECK = 3502,
    IDC_CACHE_CHECK = 3503,
    IDC_PASSWORDS_CHECK = 3504,
    IDC_EXCEPTIONS_CHECK = 3505,
    IDC_CLEAR_BTN = 3506,
    IDC_CLOSE_BTN = 3507,
    IDC_STATUS_LABEL = 3508
};

// Completion callbacks fire on the CEF UI thread; route them back into the
// status label without keeping a raw pointer past WM_DESTROY.
struct ClearUi {
    HWND owner = nullptr;
    HWND window = nullptr;
    HWND status = nullptr;
    HFONT font = nullptr;
    int pending = 0;
};

ClearUi* ui_from_hwnd(HWND hwnd) {
    return reinterpret_cast<ClearUi*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
}

class DoneCallback : public CefCompletionCallback {
public:
    explicit DoneCallback(HWND window) : window_(window) {}

    void OnComplete() override {
        ClearUi* ui = ui_from_hwnd(window_);
        if (!ui) return;
        if (--ui->pending <= 0) {
            SetWindowTextW(ui->status, L"Done.");
        } else {
            SetWindowTextW(ui->status, L"Working...");
        }
    }

private:
    HWND window_;

    IMPLEMENT_REFCOUNTING(DoneCallback);
    DISALLOW_COPY_AND_ASSIGN(DoneCallback);
};

bool check_checked(HWND hwnd, DialogControl id) {
    return SendMessageW(GetDlgItem(hwnd, id), BM_GETCHECK, 0, 0) == BST_CHECKED;
}

int window_dpi(HWND hwnd) {
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
        UINT dpi = fn(hwnd);
        if (dpi > 0) return static_cast<int>(dpi);
    }
    return 96;
}

std::wstring wide(const std::string& text) {
    if (text.empty()) return std::wstring();
    int size = MultiByteToWideChar(CP_UTF8, 0, text.c_str(),
        static_cast<int>(text.size()), nullptr, 0);
    std::wstring out(static_cast<size_t>(size), L'\0');
    if (size > 0) {
        MultiByteToWideChar(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()),
            out.data(), size);
        while (!out.empty() && out.back() == L'\0') out.pop_back();
    }
    return out;
}

void clear_selected(HWND hwnd) {
    Runtime* rt = runtime();
    if (!rt) return;
    ClearUi* ui = ui_from_hwnd(hwnd);
    if (!ui) return;

    std::string cleared;
    CefRefPtr<CefRequestContext> context = CefRequestContext::GetGlobalContext();

    if (check_checked(hwnd, IDC_HISTORY_CHECK)) {
        if (rt->history) {
            rt->history->clear();
            cleared += "history ";
        }
    }
    if (check_checked(hwnd, IDC_COOKIES_CHECK)) {
        CefRefPtr<CefCookieManager> cookies = CefCookieManager::GetGlobalManager(nullptr);
        if (cookies) {
            cookies->DeleteCookies(CefString(), CefString(), nullptr);
            cookies->FlushStore(nullptr);
            cleared += "cookies ";
        }
    }
    if (check_checked(hwnd, IDC_CACHE_CHECK)) {
        context->ClearHttpCache(new DoneCallback(hwnd));
        ++ui->pending;
        cleared += "cache ";
    }
    if (check_checked(hwnd, IDC_PASSWORDS_CHECK)) {
        context->ClearHttpAuthCredentials(new DoneCallback(hwnd));
        ++ui->pending;
        cleared += "passwords ";
    }
    if (check_checked(hwnd, IDC_EXCEPTIONS_CHECK)) {
        context->ClearCertificateExceptions(new DoneCallback(hwnd));
        ++ui->pending;
        cleared += "exceptions ";
    }

    if (cleared.empty()) {
        SetWindowTextW(ui->status, L"Nothing selected.");
        return;
    }
    SetWindowTextW(ui->status, ui->pending > 0 ? L"Working..." : L"Done.");
    InvalidateRect(hwnd, nullptr, TRUE);
}

void layout_dialog(HWND window) {
    RECT client;
    GetClientRect(window, &client);
    int width = client.right - client.left;
    int height = client.bottom - client.top;
    int dpi = window_dpi(window);
    auto sc = [dpi](int v) { return MulDiv(v, dpi, 96); };
    int m = sc(14);
    int row = sc(26);
    int y = m;
    for (DialogControl id : {IDC_HISTORY_CHECK, IDC_COOKIES_CHECK, IDC_CACHE_CHECK,
                             IDC_PASSWORDS_CHECK, IDC_EXCEPTIONS_CHECK}) {
        MoveWindow(GetDlgItem(window, id), m, y, width - m * 2, row, TRUE);
        y += row + sc(6);
    }
    y += sc(6);
    int btn_w = sc(140);
    MoveWindow(GetDlgItem(window, IDC_CLEAR_BTN), m, y, btn_w, sc(30), TRUE);
    MoveWindow(GetDlgItem(window, IDC_CLOSE_BTN), m + btn_w + sc(8), y,
        btn_w, sc(30), TRUE);
    y += sc(38);
    MoveWindow(GetDlgItem(window, IDC_STATUS_LABEL), m, y, width - m * 2, row, TRUE);
    (void)height;
}

LRESULT CALLBACK clear_data_proc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
    ClearUi* ui = nullptr;
    if (msg == WM_NCCREATE) {
        auto* create = reinterpret_cast<CREATESTRUCTW*>(lparam);
        ui = static_cast<ClearUi*>(create->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(ui));
    } else {
        ui = ui_from_hwnd(hwnd);
    }

    switch (msg) {
        case WM_SIZE:
            if (ui) layout_dialog(hwnd);
            return 0;
        case WM_ERASEBKGND:
            return 1;
        case WM_CTLCOLORSTATIC:
            return aux_static_ctlcolor(reinterpret_cast<HDC>(wparam));
        case WM_COMMAND: {
            if (!ui) break;
            switch (LOWORD(wparam)) {
                case IDC_CLEAR_BTN:
                    clear_selected(hwnd);
                    return 0;
                case IDC_CLOSE_BTN:
                case IDCANCEL:
                    DestroyWindow(hwnd);
                    return 0;
                default:
                    break;
            }
            break;
        }
        case WM_CLOSE:
            DestroyWindow(hwnd);
            return 0;
        case WM_DESTROY: {
            if (ui) {
                if (ui->owner) EnableWindow(ui->owner, TRUE);
                if (ui->font) DeleteObject(ui->font);
                delete ui;
            }
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
            return 0;
        }
        default:
            break;
    }
    return DefWindowProcW(hwnd, msg, wparam, lparam);
}

}  // namespace

void open_clear_data_window(HWND owner) {
    Runtime* rt = runtime();
    if (!rt) return;
    if (owner) EnableWindow(owner, FALSE);

    WNDCLASSW wc = {};
    wc.lpfnWndProc = clear_data_proc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.hCursor = LoadCursorW(nullptr, reinterpret_cast<LPCWSTR>(IDC_ARROW));
    wc.hbrBackground = gfx::aux_surface_brush();
    wc.lpszClassName = kDialogClass;
    RegisterClassW(&wc);

    auto* ui = new ClearUi();
    ui->owner = owner;

    int dpi = window_dpi(owner);
    int width = MulDiv(420, dpi > 0 ? dpi : 96, 96);
    int height = MulDiv(300, dpi > 0 ? dpi : 96, 96);
    RECT area = {0, 0, width, height};
    AdjustWindowRect(&area, WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU, FALSE);
    ui->window = CreateWindowExW(0, kDialogClass, L"Kiwi \u2014 clear browsing data",
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU,
        CW_USEDEFAULT, CW_USEDEFAULT, area.right - area.left,
        area.bottom - area.top, owner, nullptr, GetModuleHandleW(nullptr), ui);
    if (!ui->window) {
        delete ui;
        if (owner) EnableWindow(owner, TRUE);
        return;
    }
    if (owner) SetWindowLongPtrW(ui->window, GWLP_HWNDPARENT,
        reinterpret_cast<LONG_PTR>(owner));

    HFONT font = CreateFontW(-MulDiv(10, dpi > 0 ? dpi : 96, 72), 0, 0, 0, FW_NORMAL,
        FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
    ui->font = font;

    struct Check {
        DialogControl id;
        const wchar_t* label;
    };
    const Check checks[] = {
        {IDC_HISTORY_CHECK, L"Browsing history"},
        {IDC_COOKIES_CHECK, L"Cookies and other site data"},
        {IDC_CACHE_CHECK, L"Cached images and files"},
        {IDC_PASSWORDS_CHECK, L"Saved passwords (HTTP authentication)"},
        {IDC_EXCEPTIONS_CHECK, L"Security certificate exceptions"},
    };
    for (const Check& check : checks) {
        HWND ctrl = CreateWindowExW(0, L"BUTTON", check.label,
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX,
            0, 0, 10, 10, ui->window,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(check.id)),
            GetModuleHandleW(nullptr), nullptr);
        SendMessageW(ctrl, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
        apply_control_theme(ctrl);
    }

    auto make_button = [&](const wchar_t* text, DialogControl id) {
        HWND ctrl = CreateWindowExW(0, L"BUTTON", text,
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
            0, 0, 10, 10, ui->window,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
            GetModuleHandleW(nullptr), nullptr);
        SendMessageW(ctrl, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
        apply_control_theme(ctrl);
    };
    make_button(L"Clear selected", IDC_CLEAR_BTN);
    make_button(L"Close", IDC_CLOSE_BTN);

    ui->status = CreateWindowExW(0, L"STATIC", L"Select what to clear.",
        WS_CHILD | WS_VISIBLE, 0, 0, 10, 10, ui->window,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_STATUS_LABEL)),
        GetModuleHandleW(nullptr), nullptr);
    SendMessageW(ui->status, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);

    // History starts checked (the most common request), the rest follow.
    SendMessageW(GetDlgItem(ui->window, IDC_HISTORY_CHECK), BM_SETCHECK,
        BST_CHECKED, 0);
    SendMessageW(GetDlgItem(ui->window, IDC_COOKIES_CHECK), BM_SETCHECK,
        BST_CHECKED, 0);

    layout_dialog(ui->window);
    ShowWindow(ui->window, SW_SHOW);
    UpdateWindow(ui->window);
}

}
