#include "cef_shell_win.hpp"

#include "../cef_app.hpp"
#include "../cef_bridge.hpp"
#include "../cef_client.hpp"
#include "../cef_runtime.hpp"
#include "../../start_page.hpp"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <commctrl.h>

#include "include/cef_app.h"
#include "include/cef_browser.h"

#include <cstdint>
#include <ctime>
#include <cwchar>
#include <string>
#include <vector>

namespace kiwi {

namespace {

constexpr wchar_t kWindowClass[] = L"KiwiCefShell";
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
    ID_STATUS
};

struct WinTab {
    int browser_id = 0;
    bool private_tab = false;
    std::string title;
    std::string url;
    CefRefPtr<CefBrowser> browser;
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

private:
    static LRESULT CALLBACK wnd_proc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam);
    static LRESULT CALLBACK omnibox_proc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam,
                                         UINT_PTR id, DWORD_PTR data);

    bool create_window();

    void create_controls();
    void layout();
    void on_command(WPARAM wparam, LPARAM lparam);
    void on_draw_item(WPARAM wparam, LPARAM lparam);
    LRESULT on_notify(LPARAM lparam);

    int add_tab(const std::string& url, bool private_tab);
    void activate_index(int index);
    void close_index(int index);
    void close_active_tab();
    void navigate_entry(const std::string& text);
    void navigate_active(const std::string& url);
    void toggle_bookmark();
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

    HWND hwnd_ = nullptr;
    HWND tab_ctrl_ = nullptr;
    HWND host_ = nullptr;
    HWND omnibox_ = nullptr;
    HWND status_ = nullptr;
    HWND buttons_[5] = {};
    HFONT font_ = nullptr;

    std::vector<WinTab> tabs_;
    int active_ = -1;
    bool closing_ = false;
    CefRefPtr<CefRequestContext> private_context_;
};

int CefShell::exec() {
    if (!create_window()) return 1;
    add_tab(start_uri(false), false);
    activate_index(0);
    CefRunMessageLoop();
    return 0;
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
    const ControlId ids[5] = {ID_BACK, ID_FORWARD, ID_RELOAD, ID_NEW_TAB, ID_CLOSE_TAB};
    for (int i = 0; i < 5; ++i) {
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
        WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | TCS_TABS | TCS_SINGLELINE,
        0, 0, 10, 10, hwnd_, reinterpret_cast<HMENU>(static_cast<INT_PTR>(ID_TABS)),
        GetModuleHandleW(nullptr), nullptr);

    host_ = CreateWindowExW(0, L"STATIC", nullptr,
        WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | WS_CLIPCHILDREN,
        0, 0, 10, 10, hwnd_, reinterpret_cast<HMENU>(static_cast<INT_PTR>(ID_HOST)),
        GetModuleHandleW(nullptr), nullptr);

    status_ = CreateWindowExW(0, L"STATIC", nullptr,
        WS_CHILD | WS_VISIBLE | SS_PATHELLIPSIS,
        0, 0, 10, 10, hwnd_, reinterpret_cast<HMENU>(static_cast<INT_PTR>(ID_STATUS)),
        GetModuleHandleW(nullptr), nullptr);

    for (int i = 0; i < 5; ++i) SendMessageW(buttons_[i], WM_SETFONT, (WPARAM)font_, TRUE);
    SendMessageW(omnibox_, WM_SETFONT, (WPARAM)font_, TRUE);
    SendMessageW(tab_ctrl_, WM_SETFONT, (WPARAM)font_, TRUE);
    SendMessageW(status_, WM_SETFONT, (WPARAM)font_, TRUE);
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
        case WM_DESTROY:
            CefQuitMessageLoop();
            return 0;
        default:
            return DefWindowProcW(hwnd, msg, wparam, lparam);
    }
}

LRESULT CALLBACK CefShell::omnibox_proc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam,
                                        UINT_PTR id, DWORD_PTR data) {
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
        default:
            break;
    }
}

void CefShell::on_draw_item(WPARAM wparam, LPARAM lparam) {
    (void)wparam;
    auto* item = reinterpret_cast<DRAWITEMSTRUCT*>(lparam);
    if (!item || item->CtlType != ODT_BUTTON) return;
    bool pressed = (item->itemState & ODS_SELECTED) != 0;
    bool enabled = (item->itemState & ODS_DISABLED) == 0;
    draw_glyph(item->hDC, item->rcItem, static_cast<ControlId>(item->CtlID), enabled, pressed);
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
            if (closing_) DestroyWindow(hwnd_);
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
    for (int i = 0; i < 5; ++i) {
        MoveWindow(buttons_[i], x, tool_y, btn, btn, TRUE);
        x += btn + pad;
    }
    int omni_w = width - x - pad;
    if (omni_w < 50) omni_w = 50;
    MoveWindow(omnibox_, x, tab_h + (tool_h - omni_h) / 2, omni_w, omni_h, TRUE);

    int host_y = tab_h + tool_h;
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
        if (closing_) DestroyWindow(hwnd_);
        return;
    }
    if (active_ >= static_cast<int>(tabs_.size())) {
        activate_index(static_cast<int>(tabs_.size()) - 1);
    } else {
        if (active_ == index) {
            activate_index(active_);
        } else {
            layout();
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

int cef_shell_main() {
    CefShell shell;
    return shell.exec();
}

}
