#include "cef_shell_win.hpp"

#include "../cef_app.hpp"
#include "../cef_bridge.hpp"
#include "../cef_client.hpp"
#include "../cef_runtime.hpp"
#include "../../start_page.hpp"
#include "site_rules_win.hpp"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <commctrl.h>
#include <windowsx.h>

#include "include/cef_app.h"
#include "include/cef_browser.h"

#include "diagnostics.hpp"

#include <cstdint>
#include <ctime>
#include <cwchar>
#include <string>
#include <thread>
#include <vector>

namespace kiwi {

namespace {

constexpr wchar_t kWindowClass[] = L"KiwiCefShell";

// Self-test: a helper thread waits for the first browser to be created, then
// posts WM_APP_SELF_TEST to the shell window. The handler stores the result
// and closes the window, so `kiwi.exe --self-test` runs headless-ish on CI.
constexpr UINT WM_APP_SELF_TEST = WM_APP + 55;
constexpr unsigned kSelfTestWaitMs = 15000;
// Fail-safe default: the watchdog handler resets this to 0 only when a real
// browser exists at check time. If the window dies earlier the test stays red.
int g_self_test_exit = 2;
constexpr COLORREF kInk = RGB(44, 62, 80);
constexpr COLORREF kMuted = RGB(133, 146, 158);
constexpr COLORREF kSurface = RGB(255, 245, 240);
constexpr COLORREF kSurfaceVariant = RGB(250, 219, 216);
constexpr COLORREF kPressed = RGB(245, 203, 167);
constexpr COLORREF kOutline = RGB(237, 187, 153);

enum ControlId {
    ID_BACK = 1001,
    ID_FORWARD,
    ID_RELOAD,
    ID_NEW_TAB,
    ID_CLOSE_TAB,
    ID_OMNIBOX,
    ID_TABS,
    ID_HOST,
    ID_STATUS,
    ID_FIND_ENTRY,
    ID_FIND_PREV,
    ID_FIND_NEXT,
    ID_FIND_CLOSE,
    ID_FIND_LABEL,
    ID_SITERULES
};

struct WinTab {
    int browser_id = 0;
    bool private_tab = false;
    std::string title;
    std::string url;
    CefRefPtr<CefBrowser> browser;

    // Find-in-page session state for this tab.
    std::wstring find_text;
    int find_id = 0;
    int find_count = 0;
    int find_active = 0;
};

std::wstring wide_from_utf8(const std::string& text) {
    if (text.empty()) return std::wstring();
    int size = MultiByteToWideChar(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()),
        nullptr, 0);
    std::wstring wide(static_cast<size_t>(size), L'\0');
    if (size > 0) {
        MultiByteToWideChar(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()),
            wide.data(), size);
        while (!wide.empty() && wide.back() == L'\0') wide.pop_back();
    }
    return wide;
}

std::string utf8_from_wide(const std::wstring& wide) {
    if (wide.empty()) return std::string();
    int size = WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), static_cast<int>(wide.size()),
        nullptr, 0, nullptr, nullptr);
    std::string out(static_cast<size_t>(size), '\0');
    if (size > 0) {
        WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), static_cast<int>(wide.size()),
            out.data(), size, nullptr, nullptr);
        while (!out.empty() && out.back() == '\0') out.pop_back();
    }
    return out;
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

int scale_for(HWND window, int value) {
    return MulDiv(value, dpi_for(window), 96);
}

HBRUSH surface_brush() {
    static HBRUSH brush = CreateSolidBrush(kSurface);
    return brush;
}

void fill_rect_color(HDC dc, const RECT& rect, COLORREF color) {
    HBRUSH brush = CreateSolidBrush(color);
    FillRect(dc, &rect, brush);
    DeleteObject(brush);
}

void draw_glyph(HDC dc, const RECT& rect, ControlId id, bool enabled, bool pressed) {
    fill_rect_color(dc, rect, pressed ? kPressed : kSurface);
    HPEN pen = CreatePen(PS_SOLID, 2, enabled ? kInk : kMuted);
    HGDIOBJ old_pen = SelectObject(dc, pen);
    HGDIOBJ old_brush = SelectObject(dc, GetStockObject(NULL_BRUSH));

    int cx = (rect.left + rect.right) / 2;
    int cy = (rect.top + rect.bottom) / 2;
    int r = (rect.right - rect.left) / 2 - 7;
    if (r < 5) r = 5;

    switch (id) {
        case ID_BACK:
            MoveToEx(dc, cx + r / 2, cy, nullptr);
            LineTo(dc, cx - r / 2, cy);
            MoveToEx(dc, cx - r / 2, cy, nullptr);
            LineTo(dc, cx - r / 2 + 5, cy - 5);
            MoveToEx(dc, cx - r / 2, cy, nullptr);
            LineTo(dc, cx - r / 2 + 5, cy + 5);
            break;
        case ID_FORWARD:
            MoveToEx(dc, cx - r / 2, cy, nullptr);
            LineTo(dc, cx + r / 2, cy);
            MoveToEx(dc, cx + r / 2, cy, nullptr);
            LineTo(dc, cx + r / 2 - 5, cy - 5);
            MoveToEx(dc, cx + r / 2, cy, nullptr);
            LineTo(dc, cx + r / 2 - 5, cy + 5);
            break;
        case ID_RELOAD: {
            Arc(dc, cx - r, cy - r, cx + r, cy + r, cx + r, cy - 2, cx - 2, cy + r);
            POINT head[3] = {
                {cx - 2, cy + r + 3},
                {cx - 2, cy + r - 5},
                {cx + 5, cy + r - 1}
            };
            HBRUSH brush = CreateSolidBrush(enabled ? kInk : kMuted);
            HGDIOBJ old = SelectObject(dc, brush);
            SelectObject(dc, GetStockObject(NULL_PEN));
            Polygon(dc, head, 3);
            SelectObject(dc, old);
            DeleteObject(brush);
            break;
        }
        case ID_NEW_TAB:
            MoveToEx(dc, cx - r / 2, cy, nullptr);
            LineTo(dc, cx + r / 2, cy);
            MoveToEx(dc, cx, cy - r / 2, nullptr);
            LineTo(dc, cx, cy + r / 2);
            break;
        case ID_CLOSE_TAB:
            MoveToEx(dc, cx - 4, cy - 4, nullptr);
            LineTo(dc, cx + 4, cy + 4);
            MoveToEx(dc, cx + 4, cy - 4, nullptr);
            LineTo(dc, cx - 4, cy + 4);
            break;
        case ID_FIND_PREV:
            MoveToEx(dc, cx - 4, cy + 2, nullptr);
            LineTo(dc, cx, cy - 3);
            LineTo(dc, cx + 4, cy + 2);
            break;
        case ID_FIND_NEXT:
            MoveToEx(dc, cx - 4, cy - 2, nullptr);
            LineTo(dc, cx, cy + 3);
            LineTo(dc, cx + 4, cy - 2);
            break;
        case ID_FIND_CLOSE:
            MoveToEx(dc, cx - 3, cy - 3, nullptr);
            LineTo(dc, cx + 3, cy + 3);
            MoveToEx(dc, cx + 3, cy - 3, nullptr);
            LineTo(dc, cx - 3, cy + 3);
            break;
        case ID_SITERULES: {
            // Gear: ring + four spokes + hub.
            Arc(dc, cx - r, cy - r, cx + r, cy + r, cx + r, cy, cx, cy + r);
            Arc(dc, cx - r, cy - r, cx + r, cy + r, cx, cy - r, cx + r, cy);
            Arc(dc, cx - r, cy - r, cx + r, cy + r, cx - r, cy, cx, cy - r);
            Arc(dc, cx - r, cy - r, cx + r, cy + r, cx, cy + r, cx - r, cy);
            MoveToEx(dc, cx, cy - r - 2, nullptr);
            LineTo(dc, cx, cy - r + 2);
            MoveToEx(dc, cx, cy + r - 2, nullptr);
            LineTo(dc, cx, cy + r + 2);
            MoveToEx(dc, cx - r - 2, cy, nullptr);
            LineTo(dc, cx - r + 2, cy);
            MoveToEx(dc, cx + r - 2, cy, nullptr);
            LineTo(dc, cx + r + 2, cy);
            HBRUSH hub = CreateSolidBrush(enabled ? kInk : kMuted);
            HGDIOBJ old = SelectObject(dc, hub);
            SelectObject(dc, GetStockObject(NULL_PEN));
            Ellipse(dc, cx - 2, cy - 2, cx + 2, cy + 2);
            SelectObject(dc, old);
            DeleteObject(hub);
            break;
        }
        default:
            break;
    }

    SelectObject(dc, old_pen);
    SelectObject(dc, old_brush);
    DeleteObject(pen);
}

}  // namespace

class CefShell : public TabHost {
public:
    int exec();
    void store_geometry();
    void set_self_test(bool enabled) { self_test_ = enabled; }

private:
    static LRESULT CALLBACK wnd_proc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam);
    static LRESULT CALLBACK omnibox_proc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam,
                                         UINT_PTR id, DWORD_PTR data);
    static LRESULT CALLBACK find_entry_proc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam,
                                            UINT_PTR id, DWORD_PTR data);
    static LRESULT CALLBACK tab_ctrl_proc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam,
                                          UINT_PTR id, DWORD_PTR data);

    bool create_window();

    void create_controls();
    void layout();
    void on_command(WPARAM wparam, LPARAM lparam);
    void on_draw_item(WPARAM wparam, LPARAM lparam);
    void draw_tab_item(HDC dc, int item_id, const RECT& rect, bool selected);
    RECT tab_close_rect(const RECT& item_rect) const;
    LRESULT on_notify(LPARAM lparam);

    int add_tab(const std::string& url, bool private_tab);
    void activate_index(int index);
    void start_self_test_watchdog();
    void close_index(int index);
    void close_active_tab();
    void navigate_entry(const std::string& text);
    void navigate_active(const std::string& url);
    void toggle_bookmark();
    void toggle_devtools();
    void open_find_bar();
    void close_find_bar();
    void refresh_find_bar();
    void do_find(bool forward, bool find_next);
    void on_find_entry_changed();
    void update_find_label();
    void update_toolbar();
    void update_omnibox();
    void update_window_title();
    void sync_tab_labels();
    CefRefPtr<CefBrowser> active_browser();
    WinTab* tab_by_id(int browser_id);
    int index_by_id(int browser_id);
    std::string start_uri(bool private_tab);

    void on_created(CefRefPtr<CefBrowser> browser) override;
    void on_closed(int browser_id) override;
    void on_title(int browser_id, const std::string& title) override;
    void on_address(int browser_id, const std::string& url) override;
    void on_loading(int browser_id, bool loading) override;
    void on_status(int browser_id, const std::string& text) override;
    void on_popup(int browser_id, const std::string& url, bool private_tab) override;
    void on_hotkey(Hotkey key) override;
    void on_find_result(int browser_id, int count, int active_match,
                        bool final_update) override;

    HWND hwnd_ = nullptr;
    HWND tab_ctrl_ = nullptr;
    HWND host_ = nullptr;
    HWND omnibox_ = nullptr;
    HWND status_ = nullptr;
    HWND buttons_[6] = {};
    HFONT font_ = nullptr;

    HWND find_bar_ = nullptr;
    HWND find_entry_ = nullptr;
    HWND find_prev_ = nullptr;
    HWND find_next_ = nullptr;
    HWND find_close_ = nullptr;
    HWND find_label_ = nullptr;
    bool find_visible_ = false;
    int find_seq_ = 0;

    int hover_item_ = -1;
    bool hover_close_ = false;
    int press_close_item_ = -1;
    bool self_test_ = false;

    std::vector<WinTab> tabs_;
    int active_ = -1;
    bool closing_ = false;
    CefRefPtr<CefRequestContext> private_context_;
    CefRefPtr<DevToolsClient> devtools_client_ = new DevToolsClient();
};

int CefShell::exec() {
    trace_stage("shell: create_window begin");
    if (!create_window()) {
        trace_stage("shell: create_window FAILED");
        return 1;
    }
    trace_stage("shell: create_window ok, adding first tab");
    add_tab(start_uri(false), false);
    trace_stage("shell: first tab submitted");
    activate_index(0);
    if (self_test_) start_self_test_watchdog();
    trace_stage("shell: entering message loop");
    CefRunMessageLoop();
    trace_stage("shell: message loop left");
    return self_test_ ? g_self_test_exit : 0;
}

void CefShell::start_self_test_watchdog() {
    HWND hwnd = hwnd_;
    std::thread([hwnd]() {
        Sleep(kSelfTestWaitMs);
        if (IsWindow(hwnd)) PostMessageW(hwnd, WM_APP_SELF_TEST, 0, 0);
    }).detach();
}

void CefShell::store_geometry() {
    Runtime* rt = runtime();
    if (!rt || !hwnd_) return;
    RECT rect;
    if (!GetWindowRect(hwnd_, &rect)) return;
    int width = rect.right - rect.left;
    int height = rect.bottom - rect.top;
    if (width > 100 && height > 100) {
        rt->settings.window_width = width;
        rt->settings.window_height = height;
    }
}

bool CefShell::create_window() {
    INITCOMMONCONTROLSEX icc = {sizeof(icc), ICC_TAB_CLASSES | ICC_STANDARD_CLASSES};
    InitCommonControlsEx(&icc);

    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = wnd_proc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.hCursor = LoadCursorW(nullptr, reinterpret_cast<LPCWSTR>(IDC_ARROW));
    wc.hbrBackground = surface_brush();
    wc.lpszClassName = kWindowClass;
    if (!RegisterClassExW(&wc)) return false;

    Runtime* rt = runtime();
    int width = rt ? rt->settings.window_width : 1280;
    int height = rt ? rt->settings.window_height : 800;

    RECT area = {0, 0, width, height};
    AdjustWindowRect(&area, WS_OVERLAPPEDWINDOW, FALSE);
    hwnd_ = CreateWindowExW(0, kWindowClass, L"Kiwi", WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT, area.right - area.left, area.bottom - area.top,
        nullptr, nullptr, GetModuleHandleW(nullptr), this);
    if (!hwnd_) return false;

    font_ = CreateFontW(-MulDiv(10, dpi_for(hwnd_), 72), 0, 0, 0, FW_NORMAL, FALSE, FALSE,
        FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
        DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");

    create_controls();
    ShowWindow(hwnd_, SW_SHOW);
    UpdateWindow(hwnd_);
    return true;
}

void CefShell::create_controls() {
    const ControlId ids[6] = {ID_BACK, ID_FORWARD, ID_RELOAD, ID_NEW_TAB, ID_CLOSE_TAB,
        ID_SITERULES};
    for (int i = 0; i < 6; ++i) {
        buttons_[i] = CreateWindowExW(0, L"BUTTON", nullptr,
            WS_CHILD | WS_VISIBLE | BS_OWNERDRAW | WS_TABSTOP,
            0, 0, 10, 10, hwnd_, reinterpret_cast<HMENU>(static_cast<INT_PTR>(ids[i])),
            GetModuleHandleW(nullptr), nullptr);
    }

    omnibox_ = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", nullptr,
        WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL | ES_WANTRETURN,
        0, 0, 10, 10, hwnd_, reinterpret_cast<HMENU>(static_cast<INT_PTR>(ID_OMNIBOX)),
        GetModuleHandleW(nullptr), nullptr);
    SetWindowSubclass(omnibox_, omnibox_proc, 1, reinterpret_cast<DWORD_PTR>(this));
    SendMessageW(omnibox_, EM_SETCUEBANNER, TRUE,
        reinterpret_cast<LPARAM>(L"Search or enter address"));

    tab_ctrl_ = CreateWindowExW(0, WC_TABCONTROLW, nullptr,
        WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | TCS_TABS | TCS_SINGLELINE | TCS_OWNERDRAWFIXED,
        0, 0, 10, 10, hwnd_, reinterpret_cast<HMENU>(static_cast<INT_PTR>(ID_TABS)),
        GetModuleHandleW(nullptr), nullptr);
    SetWindowSubclass(tab_ctrl_, tab_ctrl_proc, 1, reinterpret_cast<DWORD_PTR>(this));
    // Extra horizontal padding reserves the right side of every tab for the
    // per-tab close button drawn in WM_DRAWITEM.
    SendMessageW(tab_ctrl_, TCM_SETPADDING, 0,
        MAKELPARAM(MulDiv(16, dpi_for(hwnd_), 96), MulDiv(4, dpi_for(hwnd_), 96)));

    host_ = CreateWindowExW(0, L"STATIC", nullptr,
        WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | WS_CLIPCHILDREN,
        0, 0, 10, 10, hwnd_, reinterpret_cast<HMENU>(static_cast<INT_PTR>(ID_HOST)),
        GetModuleHandleW(nullptr), nullptr);

    status_ = CreateWindowExW(0, L"STATIC", nullptr,
        WS_CHILD | WS_VISIBLE | SS_PATHELLIPSIS,
        0, 0, 10, 10, hwnd_, reinterpret_cast<HMENU>(static_cast<INT_PTR>(ID_STATUS)),
        GetModuleHandleW(nullptr), nullptr);

    for (int i = 0; i < 6; ++i) SendMessageW(buttons_[i], WM_SETFONT, (WPARAM)font_, TRUE);
    SendMessageW(omnibox_, WM_SETFONT, (WPARAM)font_, TRUE);
    SendMessageW(tab_ctrl_, WM_SETFONT, (WPARAM)font_, TRUE);
    SendMessageW(status_, WM_SETFONT, (WPARAM)font_, TRUE);

    // Find-in-page bar: hidden until Ctrl+F.
    find_entry_ = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", nullptr,
        WS_CHILD | ES_AUTOHSCROLL | ES_WANTRETURN,
        0, 0, 10, 10, hwnd_, reinterpret_cast<HMENU>(static_cast<INT_PTR>(ID_FIND_ENTRY)),
        GetModuleHandleW(nullptr), nullptr);
    SetWindowSubclass(find_entry_, find_entry_proc, 1, reinterpret_cast<DWORD_PTR>(this));
    SendMessageW(find_entry_, EM_SETCUEBANNER, TRUE,
        reinterpret_cast<LPARAM>(L"Find in page"));

    find_prev_ = CreateWindowExW(0, L"BUTTON", nullptr,
        WS_CHILD | BS_OWNERDRAW,
        0, 0, 10, 10, hwnd_,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(ID_FIND_PREV)),
        GetModuleHandleW(nullptr), nullptr);
    find_next_ = CreateWindowExW(0, L"BUTTON", nullptr,
        WS_CHILD | BS_OWNERDRAW,
        0, 0, 10, 10, hwnd_,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(ID_FIND_NEXT)),
        GetModuleHandleW(nullptr), nullptr);
    find_close_ = CreateWindowExW(0, L"BUTTON", nullptr,
        WS_CHILD | BS_OWNERDRAW,
        0, 0, 10, 10, hwnd_,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(ID_FIND_CLOSE)),
        GetModuleHandleW(nullptr), nullptr);
    find_label_ = CreateWindowExW(0, L"STATIC", nullptr,
        WS_CHILD | SS_CENTER | SS_PATHELLIPSIS,
        0, 0, 10, 10, hwnd_, reinterpret_cast<HMENU>(static_cast<INT_PTR>(ID_FIND_LABEL)),
        GetModuleHandleW(nullptr), nullptr);

    SendMessageW(find_entry_, WM_SETFONT, (WPARAM)font_, TRUE);
    SendMessageW(find_prev_, WM_SETFONT, (WPARAM)font_, TRUE);
    SendMessageW(find_next_, WM_SETFONT, (WPARAM)font_, TRUE);
    SendMessageW(find_close_, WM_SETFONT, (WPARAM)font_, TRUE);
    SendMessageW(find_label_, WM_SETFONT, (WPARAM)font_, TRUE);
}

LRESULT CALLBACK CefShell::wnd_proc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
    CefShell* self = nullptr;
    if (msg == WM_NCCREATE) {
        auto* create = reinterpret_cast<CREATESTRUCTW*>(lparam);
        self = static_cast<CefShell*>(create->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    } else {
        self = reinterpret_cast<CefShell*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    }
    if (!self) return DefWindowProcW(hwnd, msg, wparam, lparam);
    self->hwnd_ = hwnd;

    switch (msg) {
        case WM_SIZE:
            self->layout();
            return 0;
        case WM_ERASEBKGND: {
            HDC dc = reinterpret_cast<HDC>(wparam);
            RECT rect;
            GetClientRect(hwnd, &rect);
            fill_rect_color(dc, rect, kSurface);
            return 1;
        }
        case WM_CTLCOLORSTATIC: {
            HDC dc = reinterpret_cast<HDC>(wparam);
            SetBkColor(dc, kSurface);
            SetTextColor(dc, kMuted);
            return reinterpret_cast<LRESULT>(surface_brush());
        }
        case WM_CTLCOLOREDIT: {
            HDC dc = reinterpret_cast<HDC>(wparam);
            SetBkColor(dc, RGB(255, 255, 255));
            SetTextColor(dc, kInk);
            static HBRUSH white = CreateSolidBrush(RGB(255, 255, 255));
            return reinterpret_cast<LRESULT>(white);
        }
        case WM_COMMAND:
            self->on_command(wparam, lparam);
            return 0;
        case WM_DRAWITEM:
            self->on_draw_item(wparam, lparam);
            return TRUE;
        case WM_NOTIFY:
            return self->on_notify(lparam);
        case WM_CLOSE:
            self->store_geometry();
            self->closing_ = true;
            for (WinTab& tab : self->tabs_) {
                if (tab.browser) tab.browser->GetHost()->CloseBrowser(false);
            }
            if (self->tabs_.empty()) DestroyWindow(hwnd);
            return 0;
        case WM_APP_SELF_TEST:
            g_self_test_exit = self->active_browser() ? 0 : 2;
            DestroyWindow(hwnd);
            return 0;
        case WM_DESTROY:
            CefQuitMessageLoop();
            return 0;
        default:
            return DefWindowProcW(hwnd, msg, wparam, lparam);
    }
}

LRESULT CALLBACK CefShell::omnibox_proc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam,
                                        UINT_PTR id, DWORD_PTR data) {
    (void)id;
    auto* self = reinterpret_cast<CefShell*>(data);
    if (msg == WM_KEYDOWN) {
        const bool ctrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
        const bool shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
        if (wparam == VK_RETURN && !ctrl && !shift) {
            int length = GetWindowTextLengthW(hwnd);
            std::wstring text(static_cast<size_t>(length) + 1, L'\0');
            GetWindowTextW(hwnd, text.data(), length + 1);
            text.resize(static_cast<size_t>(length));
            self->navigate_entry(utf8_from_wide(text));
            return 0;
        }
        if (wparam == VK_ESCAPE && !ctrl && !shift) {
            self->update_omnibox();
            if (CefRefPtr<CefBrowser> browser = self->active_browser()) {
                SetFocus(browser->GetHost()->GetWindowHandle());
            }
            return 0;
        }
        if (ctrl && wparam == 'T' && !shift) { self->on_hotkey(HK_NEW_TAB); return 0; }
        if (ctrl && wparam == 'W' && !shift) { self->on_hotkey(HK_CLOSE_TAB); return 0; }
        if (ctrl && wparam == 'L' && !shift) {
            PostMessageW(hwnd, EM_SETSEL, 0, -1);
            return 0;
        }
        if (ctrl && wparam == 'R' && !shift) { self->on_hotkey(HK_RELOAD); return 0; }
        if (ctrl && wparam == 'D' && !shift) { self->on_hotkey(HK_BOOKMARK); return 0; }
        if (ctrl && shift && wparam == 'P') { self->on_hotkey(HK_PRIVATE_TAB); return 0; }
    }
    return DefSubclassProc(hwnd, msg, wparam, lparam);
}

LRESULT CALLBACK CefShell::find_entry_proc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam,
                                           UINT_PTR id, DWORD_PTR data) {
    (void)id;
    auto* self = reinterpret_cast<CefShell*>(data);
    if (msg == WM_KEYDOWN) {
        const bool shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
        if (wparam == VK_RETURN) {
            self->do_find(!shift, true);
            return 0;
        }
        if (wparam == VK_ESCAPE) {
            self->close_find_bar();
            return 0;
        }
    }
    return DefSubclassProc(hwnd, msg, wparam, lparam);
}

LRESULT CALLBACK CefShell::tab_ctrl_proc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam,
                                         UINT_PTR id, DWORD_PTR data) {
    (void)id;
    auto* self = reinterpret_cast<CefShell*>(data);
    switch (msg) {
        case WM_LBUTTONDOWN: {
            POINT pt = {GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam)};
            TCHITTESTINFO hit = {};
            hit.pt = pt;
            int index = static_cast<int>(SendMessageW(hwnd, TCM_HITTEST, 0,
                reinterpret_cast<LPARAM>(&hit)));
            if (index >= 0) {
                RECT item;
                SendMessageW(hwnd, TCM_GETITEMRECT, index, reinterpret_cast<LPARAM>(&item));
                RECT close = self->tab_close_rect(item);
                if (PtInRect(&close, pt)) {
                    self->press_close_item_ = index;
                    SetCapture(hwnd);
                    InvalidateRect(hwnd, nullptr, TRUE);
                    return 0;  // keep the tab selected while the close button is pressed
                }
            }
            break;
        }
        case WM_LBUTTONUP: {
            if (self->press_close_item_ >= 0) {
                int index = self->press_close_item_;
                self->press_close_item_ = -1;
                if (GetCapture() == hwnd) ReleaseCapture();
                POINT pt = {GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam)};
                RECT item;
                SendMessageW(hwnd, TCM_GETITEMRECT, index, reinterpret_cast<LPARAM>(&item));
                RECT close = self->tab_close_rect(item);
                if (pt.x >= item.left && pt.x <= item.right && pt.y >= item.top
                    && pt.y <= item.bottom && PtInRect(&close, pt)) {
                    self->close_index(index);
                }
                InvalidateRect(hwnd, nullptr, TRUE);
                return 0;
            }
            break;
        }
        case WM_MOUSEMOVE: {
            TRACKMOUSEEVENT track = {sizeof(track), TME_LEAVE, hwnd, 0};
            TrackMouseEvent(&track);
            POINT pt = {GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam)};
            TCHITTESTINFO hit = {};
            hit.pt = pt;
            int index = static_cast<int>(SendMessageW(hwnd, TCM_HITTEST, 0,
                reinterpret_cast<LPARAM>(&hit)));
            bool close = false;
            if (index >= 0) {
                RECT item;
                SendMessageW(hwnd, TCM_GETITEMRECT, index, reinterpret_cast<LPARAM>(&item));
                RECT close_rect = self->tab_close_rect(item);
                close = PtInRect(&close_rect, pt) != FALSE;
            }
            if (index != self->hover_item_ || close != self->hover_close_) {
                self->hover_item_ = index;
                self->hover_close_ = close;
                InvalidateRect(hwnd, nullptr, TRUE);
            }
            break;
        }
        case WM_MOUSELEAVE:
            if (self->hover_item_ != -1 || self->hover_close_) {
                self->hover_item_ = -1;
                self->hover_close_ = false;
                InvalidateRect(hwnd, nullptr, TRUE);
            }
            break;
        case WM_CAPTURECHANGED:
            self->press_close_item_ = -1;
            break;
        default:
            break;
    }
    return DefSubclassProc(hwnd, msg, wparam, lparam);
}

void CefShell::on_command(WPARAM wparam, LPARAM lparam) {
    (void)lparam;
    switch (LOWORD(wparam)) {
        case ID_BACK: {
            if (CefRefPtr<CefBrowser> browser = active_browser()) browser->GoBack();
            break;
        }
        case ID_FORWARD: {
            if (CefRefPtr<CefBrowser> browser = active_browser()) browser->GoForward();
            break;
        }
        case ID_RELOAD:
            on_hotkey(HK_RELOAD);
            break;
        case ID_NEW_TAB:
            on_hotkey(HK_NEW_TAB);
            break;
        case ID_CLOSE_TAB:
            on_hotkey(HK_CLOSE_TAB);
            break;
        case ID_SITERULES:
            open_site_rules_dialog(hwnd_);
            break;
        case ID_FIND_ENTRY:
            if (HIWORD(wparam) == EN_CHANGE) on_find_entry_changed();
            break;
        case ID_FIND_PREV:
            do_find(false, true);
            break;
        case ID_FIND_NEXT:
            do_find(true, true);
            break;
        case ID_FIND_CLOSE:
            close_find_bar();
            break;
        default:
            break;
    }
}

void CefShell::on_draw_item(WPARAM wparam, LPARAM lparam) {
    (void)wparam;
    auto* item = reinterpret_cast<DRAWITEMSTRUCT*>(lparam);
    if (!item) return;
    if (item->CtlType == ODT_BUTTON) {
        bool pressed = (item->itemState & ODS_SELECTED) != 0;
        bool enabled = (item->itemState & ODS_DISABLED) == 0;
        draw_glyph(item->hDC, item->rcItem, static_cast<ControlId>(item->CtlID), enabled, pressed);
        return;
    }
    if (item->CtlType == ODT_TAB) {
        draw_tab_item(item->hDC, static_cast<int>(item->itemID), item->rcItem,
            (item->itemState & ODS_SELECTED) != 0);
    }
}

void CefShell::draw_tab_item(HDC dc, int item_id, const RECT& rect, bool selected) {
    fill_rect_color(dc, rect, selected ? kSurfaceVariant : kSurface);

    wchar_t text[256] = {};
    TCITEMW info = {};
    info.mask = TCIF_TEXT;
    info.pszText = text;
    info.cchTextMax = 256;
    SendMessageW(tab_ctrl_, TCM_GETITEMW, static_cast<WPARAM>(item_id),
        reinterpret_cast<LPARAM>(&info));

    int dpi = dpi_for(hwnd_);
    RECT close = tab_close_rect(rect);
    bool close_hot = (hover_item_ == item_id && hover_close_) || press_close_item_ == item_id;
    if (close_hot) fill_rect_color(dc, close, kPressed);

    RECT text_rect = rect;
    text_rect.left += MulDiv(8, dpi, 96);
    text_rect.right = close.left - MulDiv(2, dpi, 96);
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, selected ? kInk : kMuted);
    DrawTextW(dc, text, -1, &text_rect,
        DT_END_ELLIPSIS | DT_SINGLELINE | DT_VCENTER | DT_LEFT);

    HPEN pen = CreatePen(PS_SOLID, 1, kOutline);
    HGDIOBJ old_pen = SelectObject(dc, pen);
    HGDIOBJ old_brush = SelectObject(dc, GetStockObject(NULL_BRUSH));
    if (selected) Rectangle(dc, rect.left, rect.top, rect.right, rect.bottom);

    int cx = (close.left + close.right) / 2;
    int cy = (close.top + close.bottom) / 2;
    int r = (close.right - close.left) / 2 - MulDiv(2, dpi, 96);
    if (r < 3) r = 3;
    HPEN close_pen = CreatePen(PS_SOLID, 1, close_hot ? kInk : kMuted);
    SelectObject(dc, close_pen);
    MoveToEx(dc, cx - r, cy - r, nullptr);
    LineTo(dc, cx + r, cy + r);
    MoveToEx(dc, cx + r, cy - r, nullptr);
    LineTo(dc, cx - r, cy + r);
    SelectObject(dc, old_pen);
    SelectObject(dc, old_brush);
    DeleteObject(close_pen);
    DeleteObject(pen);
}

RECT CefShell::tab_close_rect(const RECT& item_rect) const {
    int dpi = dpi_for(hwnd_);
    int size = MulDiv(14, dpi, 96);
    if (size > item_rect.bottom - item_rect.top - 4) {
        size = item_rect.bottom - item_rect.top - 4;
    }
    if (size < 8) size = 8;
    RECT rect;
    rect.right = item_rect.right - MulDiv(4, dpi, 96);
    rect.left = rect.right - size;
    rect.top = (item_rect.top + item_rect.bottom - size) / 2;
    rect.bottom = rect.top + size;
    return rect;
}

LRESULT CefShell::on_notify(LPARAM lparam) {
    auto* header = reinterpret_cast<NMHDR*>(lparam);
    if (header->idFrom == ID_TABS && header->code == TCN_SELCHANGE) {
        int selected = static_cast<int>(SendMessageW(tab_ctrl_, TCM_GETCURSEL, 0, 0));
        activate_index(selected);
    }
    return 0;
}

std::string CefShell::start_uri(bool private_tab) {
    std::string uri = runtime()->start_page_uri(private_tab);
    return uri.empty() ? std::string("https://example.org") : uri;
}

int CefShell::add_tab(const std::string& url, bool private_tab) {
    WinTab tab;
    tab.private_tab = private_tab;
    tab.url = url;
    tabs_.push_back(std::move(tab));

    CefRefPtr<KiwiClient> client = new KiwiClient(this, private_tab);
    CefRefPtr<CefRequestContext> context;
    if (private_tab) {
        if (!private_context_) {
            CefRequestContextSettings settings;
            private_context_ = CefRequestContext::CreateContext(settings, nullptr);
        }
        context = private_context_;
    } else {
        context = CefRequestContext::GetGlobalContext();
    }

    CefWindowInfo info;
    info.SetAsChild(host_, CefRect(0, 0, 10, 10));
    CefBrowserSettings browser_settings;
    Runtime* rt = runtime();
    if (rt && !rt->settings.javascript_enabled) {
        browser_settings.javascript = STATE_DISABLED;
    }

    TCITEMW item = {};
    item.mask = TCIF_TEXT;
    const wchar_t* label = private_tab ? L"New private tab" : L"New tab";
    item.pszText = const_cast<LPWSTR>(label);
    int position = static_cast<int>(tabs_.size()) - 1;
    SendMessageW(tab_ctrl_, TCM_INSERTITEMW, position, reinterpret_cast<LPARAM>(&item));

    CefBrowserHost::CreateBrowser(info, client, url, browser_settings, nullptr, context);
    return position;
}

void CefShell::activate_index(int index) {
    if (index < 0 || index >= static_cast<int>(tabs_.size())) return;
    active_ = index;
    SendMessageW(tab_ctrl_, TCM_SETCURSEL, index, 0);
    layout();
    update_omnibox();
    update_window_title();
    update_toolbar();
    if (CefRefPtr<CefBrowser> browser = active_browser()) {
        SetFocus(browser->GetHost()->GetWindowHandle());
    }
    if (find_visible_) refresh_find_bar();
}

void CefShell::close_index(int index) {
    if (index < 0 || index >= static_cast<int>(tabs_.size())) return;
    WinTab& tab = tabs_[index];
    if (tab.browser) {
        tab.browser->GetHost()->CloseBrowser(false);
    } else {
        tabs_.erase(tabs_.begin() + index);
        SendMessageW(tab_ctrl_, TCM_DELETEITEM, index, 0);
        if (tabs_.empty()) {
            DestroyWindow(hwnd_);  // closing the last tab closes the browser
        } else if (active_ >= static_cast<int>(tabs_.size())) {
            activate_index(static_cast<int>(tabs_.size()) - 1);
        }
    }
}

void CefShell::close_active_tab() {
    close_index(active_);
}

void CefShell::navigate_entry(const std::string& text) {
    Runtime* rt = runtime();
    if (!rt) return;
    std::string url;
    if (is_start_page_entry(text)) {
        url = start_uri(false);
    } else {
        url = mustela::resolve_entry(text, rt->settings);
    }
    if (url.empty()) return;
    navigate_active(url);
}

void CefShell::navigate_active(const std::string& url) {
    CefRefPtr<CefBrowser> browser = active_browser();
    if (!browser) return;
    browser->GetMainFrame()->LoadURL(url);
}

void CefShell::toggle_bookmark() {
    Runtime* rt = runtime();
    if (!rt || active_ < 0 || active_ >= static_cast<int>(tabs_.size())) return;
    WinTab* tab = &tabs_[active_];
    if (tab->url.empty() || is_start_page_entry(tab->url)) return;
    if (rt->bookmarks->contains(tab->url)) {
        rt->bookmarks->remove(tab->url);
    } else {
        rt->bookmarks->add(tab->url, tab->title, std::time(nullptr));
    }
}

void CefShell::toggle_devtools() {
    CefRefPtr<CefBrowser> browser = active_browser();
    if (!browser) return;
    CefRefPtr<CefBrowserHost> host = browser->GetHost();
    if (host->HasDevTools()) {
        host->CloseDevTools();
    } else {
        // Empty window info: CEF creates its own popup window for the
        // DevTools browser; DevToolsClient keeps it out of the tab strip.
        host->ShowDevTools(CefWindowInfo(), devtools_client_, CefBrowserSettings(),
            CefPoint());
    }
}

void CefShell::open_find_bar() {
    find_visible_ = true;
    ShowWindow(find_entry_, SW_SHOW);
    ShowWindow(find_prev_, SW_SHOW);
    ShowWindow(find_next_, SW_SHOW);
    ShowWindow(find_close_, SW_SHOW);
    ShowWindow(find_label_, SW_SHOW);
    layout();
    refresh_find_bar();
}

void CefShell::close_find_bar() {
    if (CefRefPtr<CefBrowser> browser = active_browser()) {
        browser->GetHost()->StopFinding(true);
    }
    find_visible_ = false;
    ShowWindow(find_entry_, SW_HIDE);
    ShowWindow(find_prev_, SW_HIDE);
    ShowWindow(find_next_, SW_HIDE);
    ShowWindow(find_close_, SW_HIDE);
    ShowWindow(find_label_, SW_HIDE);
    layout();
    if (CefRefPtr<CefBrowser> browser = active_browser()) {
        SetFocus(browser->GetHost()->GetWindowHandle());
    }
}

void CefShell::refresh_find_bar() {
    if (!find_visible_) return;
    if (active_ < 0 || active_ >= static_cast<int>(tabs_.size())) return;
    WinTab& tab = tabs_[active_];
    SetWindowTextW(find_entry_, tab.find_text.c_str());
    update_find_label();
    SetFocus(find_entry_);
    SendMessageW(find_entry_, EM_SETSEL, 0, -1);
}

void CefShell::do_find(bool forward, bool find_next) {
    if (active_ < 0 || active_ >= static_cast<int>(tabs_.size())) return;
    WinTab& tab = tabs_[active_];
    if (tab.find_text.empty()) return;
    CefRefPtr<CefBrowser> browser = tab.browser;
    if (!browser) return;
    tab.find_id = ++find_seq_;
    browser->GetHost()->Find(tab.find_text, forward, false, find_next ? TRUE : FALSE);
}

void CefShell::on_find_entry_changed() {
    if (active_ < 0 || active_ >= static_cast<int>(tabs_.size())) return;
    WinTab& tab = tabs_[active_];
    int length = GetWindowTextLengthW(find_entry_);
    std::wstring text(static_cast<size_t>(length) + 1, L'\0');
    GetWindowTextW(find_entry_, text.data(), length + 1);
    text.resize(static_cast<size_t>(length));
    tab.find_text = text;
    if (text.empty()) {
        tab.find_count = 0;
        tab.find_active = 0;
        if (CefRefPtr<CefBrowser> browser = tab.browser) {
            browser->GetHost()->StopFinding(true);
        }
        update_find_label();
        return;
    }
    do_find(true, false);
}

void CefShell::update_find_label() {
    if (!find_label_) return;
    wchar_t text[64] = L"";
    if (active_ >= 0 && active_ < static_cast<int>(tabs_.size())) {
        const WinTab& tab = tabs_[active_];
        if (!tab.find_text.empty()) {
            if (tab.find_count > 0) {
                swprintf(text, 64, L"%d of %d", tab.find_active, tab.find_count);
            } else {
                swprintf(text, 64, L"No matches");
            }
        }
    }
    SetWindowTextW(find_label_, text);
}

void CefShell::update_toolbar() {
    CefRefPtr<CefBrowser> browser = active_browser();
    bool back = browser && browser->CanGoBack();
    bool forward = browser && browser->CanGoForward();
    EnableWindow(buttons_[0], back ? TRUE : FALSE);
    EnableWindow(buttons_[1], forward ? TRUE : FALSE);
}

void CefShell::update_omnibox() {
    if (active_ < 0 || active_ >= static_cast<int>(tabs_.size())) return;
    if (GetFocus() == omnibox_) return;
    const std::string& url = tabs_[active_].url;
    SetWindowTextW(omnibox_, wide_from_utf8(
        is_start_page_entry(url) ? std::string() : url).c_str());
}

void CefShell::update_window_title() {
    std::wstring title = L"Kiwi";
    if (active_ >= 0 && active_ < static_cast<int>(tabs_.size())) {
        const std::string& text = tabs_[active_].title;
        if (!text.empty()) title += L" \u2014 " + wide_from_utf8(text);
        else if (tabs_[active_].private_tab) title += L" \u2014 private tab";
    }
    SetWindowTextW(hwnd_, title.c_str());
}

void CefShell::sync_tab_labels() {
    for (size_t i = 0; i < tabs_.size(); ++i) {
        std::wstring label = wide_from_utf8(
            tabs_[i].title.empty() ? std::string(tabs_[i].private_tab ? "private" : "New tab")
                                   : tabs_[i].title);
        if (tabs_[i].private_tab) label = L"[private] " + label;
        if (label.size() > 40) {
            label.resize(40);
            label += L"\u2026";
        }
        TCITEMW item = {};
        item.mask = TCIF_TEXT;
        item.pszText = label.data();
        SendMessageW(tab_ctrl_, TCM_SETITEMW, static_cast<WPARAM>(i),
            reinterpret_cast<LPARAM>(&item));
    }
}

CefRefPtr<CefBrowser> CefShell::active_browser() {
    if (active_ < 0 || active_ >= static_cast<int>(tabs_.size())) return nullptr;
    return tabs_[active_].browser;
}

WinTab* CefShell::tab_by_id(int browser_id) {
    for (WinTab& tab : tabs_) {
        if (tab.browser_id == browser_id) return &tab;
    }
    return nullptr;
}

int CefShell::index_by_id(int browser_id) {
    for (size_t i = 0; i < tabs_.size(); ++i) {
        if (tabs_[i].browser_id == browser_id) return static_cast<int>(i);
    }
    return -1;
}

void CefShell::layout() {
    if (!hwnd_ || !tab_ctrl_ || !host_) return;
    RECT client;
    GetClientRect(hwnd_, &client);
    int width = client.right - client.left;
    int height = client.bottom - client.top;
    int dpi = dpi_for(hwnd_);
    int tab_h = MulDiv(28, dpi, 96);
    int tool_h = MulDiv(46, dpi, 96);
    int status_h = MulDiv(24, dpi, 96);
    int pad = MulDiv(6, dpi, 96);
    int btn = MulDiv(34, dpi, 96);
    int omni_h = MulDiv(28, dpi, 96);

    MoveWindow(tab_ctrl_, 0, 0, width, tab_h, TRUE);
    int x = pad;
    int tool_y = tab_h + (tool_h - btn) / 2;
    for (int i = 0; i < 6; ++i) {
        MoveWindow(buttons_[i], x, tool_y, btn, btn, TRUE);
        x += btn + pad;
    }
    int omni_w = width - x - pad;
    if (omni_w < 50) omni_w = 50;
    MoveWindow(omnibox_, x, tab_h + (tool_h - omni_h) / 2, omni_w, omni_h, TRUE);

    int host_y = tab_h + tool_h;
    if (find_visible_) {
        int find_h = MulDiv(34, dpi, 96);
        int btn_w = MulDiv(28, dpi, 96);
        int label_w = MulDiv(72, dpi, 96);
        int x_close = width - pad - btn_w;
        int x_next = x_close - pad - btn_w;
        int x_prev = x_next - pad - btn_w;
        int x_label = x_prev - pad - label_w;
        int entry_w = x_label - 2 * pad;
        if (entry_w < 40) entry_w = 40;
        int entry_y = host_y + (find_h - omni_h) / 2;
        MoveWindow(find_entry_, pad, entry_y, entry_w, omni_h, TRUE);
        MoveWindow(find_label_, x_label, entry_y, label_w, omni_h, TRUE);
        MoveWindow(find_prev_, x_prev, host_y + (find_h - btn_w) / 2, btn_w, btn_w, TRUE);
        MoveWindow(find_next_, x_next, host_y + (find_h - btn_w) / 2, btn_w, btn_w, TRUE);
        MoveWindow(find_close_, x_close, host_y + (find_h - btn_w) / 2, btn_w, btn_w, TRUE);
        host_y += find_h;
    }
    int host_h = height - host_y - status_h;
    MoveWindow(host_, 0, host_y, width, host_h > 0 ? host_h : 0, TRUE);
    MoveWindow(status_, 0, height - status_h, width, status_h, TRUE);

    for (int i = 0; i < static_cast<int>(tabs_.size()); ++i) {
        if (!tabs_[i].browser) continue;
        HWND browser_hwnd = tabs_[i].browser->GetHost()->GetWindowHandle();
        if (i == active_) {
            MoveWindow(browser_hwnd, 0, 0, width, host_h > 0 ? host_h : 0, TRUE);
            ShowWindow(browser_hwnd, SW_SHOW);
        } else {
            ShowWindow(browser_hwnd, SW_HIDE);
        }
    }
}

void CefShell::on_created(CefRefPtr<CefBrowser> browser) {
    for (WinTab& tab : tabs_) {
        if (!tab.browser) {
            tab.browser = browser;
            tab.browser_id = browser->GetIdentifier();
            if (!closing_) {
                active_ = static_cast<int>(&tab - tabs_.data());
                SendMessageW(tab_ctrl_, TCM_SETCURSEL, active_, 0);
            }
            layout();
            update_toolbar();
            if (find_visible_) refresh_find_bar();
            return;
        }
    }
}

void CefShell::on_closed(int browser_id) {
    int index = index_by_id(browser_id);
    if (index < 0) return;
    tabs_.erase(tabs_.begin() + index);
    SendMessageW(tab_ctrl_, TCM_DELETEITEM, index, 0);
    if (tabs_.empty()) {
        active_ = -1;
        DestroyWindow(hwnd_);  // closing the last tab closes the browser
        return;
    }
    if (active_ >= static_cast<int>(tabs_.size())) {
        activate_index(static_cast<int>(tabs_.size()) - 1);
    } else {
        if (active_ == index) {
            activate_index(active_);
        } else {
            layout();
            if (find_visible_) refresh_find_bar();
        }
    }
}

void CefShell::on_title(int browser_id, const std::string& title) {
    WinTab* tab = tab_by_id(browser_id);
    if (!tab) return;
    tab->title = title;
    sync_tab_labels();
    int index = index_by_id(browser_id);
    if (index == active_) update_window_title();
}

void CefShell::on_address(int browser_id, const std::string& url) {
    WinTab* tab = tab_by_id(browser_id);
    if (!tab) return;
    tab->url = url;
    int index = index_by_id(browser_id);
    if (index == active_) {
        update_omnibox();
        update_toolbar();
    }
}

void CefShell::on_loading(int browser_id, bool loading) {
    (void)browser_id;
    (void)loading;
    update_toolbar();
}

void CefShell::on_status(int browser_id, const std::string& text) {
    (void)browser_id;
    SetWindowTextW(status_, wide_from_utf8(text).c_str());
}

void CefShell::on_find_result(int browser_id, int count, int active_match,
                              bool final_update) {
    (void)final_update;
    WinTab* tab = tab_by_id(browser_id);
    if (!tab) return;
    tab->find_count = count;
    tab->find_active = active_match;
    if (active_ >= 0 && active_ < static_cast<int>(tabs_.size())
        && tabs_[active_].browser_id == browser_id) {
        update_find_label();
    }
}

void CefShell::on_popup(int browser_id, const std::string& url, bool private_tab) {
    (void)browser_id;
    int index = add_tab(url, private_tab);
    activate_index(index);
}

void CefShell::on_hotkey(Hotkey key) {
    switch (key) {
        case HK_NEW_TAB: {
            int index = add_tab(start_uri(false), false);
            activate_index(index);
            break;
        }
        case HK_PRIVATE_TAB: {
            Runtime* rt = runtime();
            if (rt && !rt->settings.private_tabs_enabled) {
                SetWindowTextW(status_, L"Private tabs are disabled in settings");
                break;
            }
            int index = add_tab(start_uri(true), true);
            activate_index(index);
            break;
        }
        case HK_CLOSE_TAB:
            close_active_tab();
            break;
        case HK_FOCUS_OMNIBOX:
            SetFocus(omnibox_);
            PostMessageW(omnibox_, EM_SETSEL, 0, -1);
            break;
        case HK_RELOAD: {
            if (CefRefPtr<CefBrowser> browser = active_browser()) browser->Reload();
            break;
        }
        case HK_BOOKMARK:
            toggle_bookmark();
            break;
        case HK_FIND:
            open_find_bar();
            break;
        case HK_FIND_NEXT:
            if (!find_visible_) open_find_bar();
            else do_find(true, true);
            break;
        case HK_FIND_PREV:
            if (!find_visible_) open_find_bar();
            else do_find(false, true);
            break;
        case HK_DEVTOOLS:
            toggle_devtools();
            break;
        case HK_BACK: {
            if (CefRefPtr<CefBrowser> browser = active_browser()) browser->GoBack();
            break;
        }
        case HK_FORWARD: {
            if (CefRefPtr<CefBrowser> browser = active_browser()) browser->GoForward();
            break;
        }
        case HK_NONE:
        default:
            break;
    }
}

int cef_shell_main(bool self_test) {
    CefShell shell;
    shell.set_self_test(self_test);
    return shell.exec();
}

int cef_self_test_exit_code() {
    return g_self_test_exit;
}

}
