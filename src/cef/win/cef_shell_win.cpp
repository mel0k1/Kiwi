#include "cef_shell_win.hpp"

#include "../cef_app.hpp"
#include "../cef_bridge.hpp"
#include "../cef_client.hpp"
#include "../cef_runtime.hpp"
#include "session.hpp"
#include "../../start_page.hpp"
#include "../../style_store.hpp"
#include "../../shields_store.hpp"
#include "../../workspaces_store.hpp"
#include "bookmarks_win.hpp"
#include "favicon_store.hpp"
#include "gfx_win.hpp"
#include "history_win.hpp"
#include "settings_win.hpp"
#include "shields_win.hpp"
#include "site_rules_win.hpp"
#include "workspaces_win.hpp"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <commctrl.h>
#include <windowsx.h>
#include <dwmapi.h>

#include "include/cef_app.h"
#include "include/cef_browser.h"

#include "diagnostics.hpp"

#include <algorithm>
#include <cstdint>
#include <ctime>
#include <cwchar>
#include <map>
#include <string>
#include <thread>
#include <vector>

namespace kiwi {

using gfx::fill_round_rect;
using gfx::stroke_round_rect;

class CefShell;
std::vector<CefShell*>& shell_windows();

namespace {

constexpr wchar_t kWindowClass[] = L"KiwiCefShell";

constexpr UINT WM_APP_SELF_TEST = WM_APP + 55;
// workspaces_win.cpp pokes every shell window through sync_workspaces()
// after the manager edits the workspace list.
constexpr UINT WM_APP_SYNC_WS = WM_APP + 56;
// Shield popover commands (TrackPopupMenu over the omnibox button).
constexpr int kShldToggle = 1;
constexpr int kShldPage = 2;
constexpr int kShldTotal = 3;
constexpr int kShldGlobal = 4;
constexpr int kShldCosmetic = 5;
constexpr int kShldExceptions = 6;
constexpr unsigned kSelfTestWaitMs = 15000;
constexpr int kSpinTimer = 3;
int g_self_test_exit = 2;

// DWM attributes that may be missing from older SDK headers.
constexpr DWORD kDwmwaUseImmersiveDarkMode = 20;
constexpr DWORD kDwmwaWindowCornerPreference = 33;
constexpr DWORD kDwmwaSystemBackdropType = 38;
constexpr DWORD kDwmcpRound = 2;
// DWM_SYSTEMBACKDROP_TYPE: 1 = none, 2 = Mica, 3 = Acrylic (frosted glass).
// (0.16.0 stored 1 as "mica" — that value actually disabled the backdrop.)
constexpr DWORD kDwmsbtNone = 1;
constexpr DWORD kDwmsbtMica = 2;
constexpr DWORD kDwmsbtAcrylic = 3;

#ifndef HTCLOSE
#define HTCLOSE 20
#endif

enum ContextMenuId {
    TABCTX_NEW = 1,
    TABCTX_RELOAD,
    TABCTX_DUPLICATE,
    TABCTX_DETACH,
    TABCTX_CLOSE,
    TABCTX_CLOSE_OTHERS,
    // "Move to workspace" submenu items: TABCTX_WS_BASE + index into the
    // shell's workspace list (guarded by kMaxWorkspaces).
    TABCTX_WS_BASE = 100
};

enum DevToolsDock {
    DOCK_BOTTOM = 0,
    DOCK_LEFT = 1,
    DOCK_RIGHT = 2
};

constexpr int kDockMenuBottom = 1;
constexpr int kDockMenuLeft = 2;
constexpr int kDockMenuRight = 3;
constexpr int kDockMenuClose = 4;

struct WinTab {
    int browser_id = 0;
    bool private_tab = false;
    std::string title;
    std::string url;
    std::string favicon_host;
    bool loading = false;
    CefRefPtr<CefBrowser> browser;

    // Opera-style workspaces: every tab belongs to exactly one workspace;
    // the strip/rail shows only the active workspace's tabs.
    int workspace = 1;
    // Kiwi Shields: network hits cancelled on the committed page.
    int shield_count = 0;

    std::wstring find_text;
    int find_id = 0;
    int find_count = 0;
    int find_active = 0;
    bool wheel_subclassed = false;
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

void fill_rect_color(HDC dc, const RECT& rect, COLORREF color) {
    HBRUSH brush = CreateSolidBrush(color);
    FillRect(dc, &rect, brush);
    DeleteObject(brush);
}

struct DwmApi {
    HMODULE lib = nullptr;
    HRESULT(WINAPI* set_attr)(HWND, DWORD, LPCVOID, DWORD) = nullptr;
    HRESULT(WINAPI* extend_frame)(HWND, const MARGINS*) = nullptr;
    bool ok() const { return lib && set_attr && extend_frame; }
};

DwmApi load_dwm() {
    DwmApi api;
    api.lib = LoadLibraryW(L"dwmapi.dll");
    if (!api.lib) return api;
    api.set_attr = reinterpret_cast<decltype(api.set_attr)>(
        reinterpret_cast<std::uintptr_t>(GetProcAddress(api.lib, "DwmSetWindowAttribute")));
    api.extend_frame = reinterpret_cast<decltype(api.extend_frame)>(
        reinterpret_cast<std::uintptr_t>(
            GetProcAddress(api.lib, "DwmExtendFrameIntoClientArea")));
    return api;
}

const DwmApi& dwm() {
    static DwmApi api = load_dwm();
    return api;
}

int windows_build() {
    using RtlGetVersionFn = LONG(WINAPI*)(PRTL_OSVERSIONINFOW);
    static int build = []() -> int {
        auto fn = reinterpret_cast<RtlGetVersionFn>(reinterpret_cast<std::uintptr_t>(
            GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "RtlGetVersion")));
        if (!fn) return 0;
        RTL_OSVERSIONINFOW info = {};
        info.dwOSVersionInfoSize = sizeof(info);
        if (fn(&info) != 0) return 0;
        return static_cast<int>(info.dwBuildNumber);
    }();
    return build;
}

HMODULE this_module() {
    static HMODULE mod = nullptr;
    if (!mod) {
        GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS
            | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
            reinterpret_cast<LPCWSTR>(&this_module), &mod);
    }
    return mod;
}

HICON app_icon(int px) {
    return static_cast<HICON>(LoadImageW(this_module(), MAKEINTRESOURCEW(1),
        IMAGE_ICON, px, px, LR_DEFAULTCOLOR));
}

// Keeps a tear-off window inside the monitor work area of the cursor.
void clamp_to_workarea(POINT& pos, int width, int height) {
    HMONITOR monitor = MonitorFromPoint(pos, MONITOR_DEFAULTTONEAREST);
    MONITORINFO info = {sizeof(info)};
    if (!GetMonitorInfoW(monitor, &info)) return;
    const RECT& work = info.rcWork;
    if (pos.x + width > work.right) pos.x = work.right - width;
    if (pos.y + height > work.bottom) pos.y = work.bottom - height;
    if (pos.x < work.left) pos.x = work.left;
    if (pos.y < work.top) pos.y = work.top;
}

}  // namespace

class CefShell : public TabHost {
public:
    int exec();
    void store_geometry();
    void set_self_test(bool enabled) { self_test_ = enabled; }
    void set_primary(bool primary) { primary_ = primary; }
    HWND main_hwnd() const { return hwnd_; }
    void open_in_new_tab(const std::string& url);

private:
    static LRESULT CALLBACK wnd_proc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam);
    static LRESULT CALLBACK omnibox_proc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam,
                                         UINT_PTR id, DWORD_PTR data);
    static LRESULT CALLBACK find_entry_proc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam,
                                            UINT_PTR id, DWORD_PTR data);
    static LRESULT CALLBACK browser_wheel_proc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam,
                                               UINT_PTR id, DWORD_PTR data);

    bool create_window();
    void create_controls();
    void layout();

    bool backdrop_active() const;
    void apply_backdrop();
    void reload_style();
    void on_command(WPARAM wparam, LPARAM lparam);
    void open_tab_context_menu(int index, const POINT& screen);

    void paint(HDC dc, const RECT& client);
    void draw_chrome(HDC dc, const RECT& client, bool direct);
    void draw_caption_buttons(HDC dc);
    void draw_tool_buttons(HDC dc, bool direct);
    void draw_omnibox(HDC dc);
    void draw_tabs(HDC dc);
    void draw_tab(HDC dc, int index, const RECT& chip);
    void draw_ws_chips(HDC dc);
    void draw_rail(HDC dc);
    void draw_devtools_split(HDC dc);
    void draw_status(HDC dc);
    void draw_find_bar(HDC dc);
    void draw_add_button(HDC dc, const RECT& rect);

    RECT tab_chip_rect(int index, int tab_w) const;
    RECT tab_close_rect(const RECT& chip) const;
    RECT add_btn_rect() const;
    int tab_width_for(int n) const;
    int visible_tab_count() const;
    int tab_at(const POINT& client_pt) const;
    int tool_at(const POINT& client_pt) const;
    bool chrome_is_drag_zone(const POINT& client_pt) const;

    int max_tab_scroll() const;
    void clamp_tab_scroll();
    void ensure_tab_visible(int index);
    void scroll_tabs(int delta);
    RECT tab_thumb_rect() const;
    RECT rail_grip_rect() const;
    void flip_rail_side(const std::string& side);
    int drop_slot_at(const POINT& client_pt) const;
    RECT drag_ghost_rect() const;
    void move_tab(int from, int to);
    bool devtools_docked_visible() const;
    bool devtools_bar_hit(const POINT& client_pt) const;
    void set_devtools_dock(int dock);
    void open_devtools_dock_menu(const POINT& screen);
    bool tear_zone_at(const POINT& client_pt) const;
    RECT tear_ghost_rect() const;
    void tear_off_window(int index, const POINT& screen);
    void adopt_tab(WinTab tab);
    void show_tear_hint();
    void move_tear_hint(const POINT& client_pt);
    void hide_tear_hint();

    void sync_spin_timer();
    void set_tab_loading(int browser_id, bool loading);
    void update_favicon_host(WinTab& tab);

    void save_session_now();
    bool restore_session();

    // ---- Opera-style workspaces ---------------------------------------
    void load_workspaces_state();
    std::vector<int> ws_tabs() const;      // global indices of the active ws
    int ws_tab_count() const;
    int slot_of_tab(int global_index) const;
    int ws_index_of(int ws_id) const;
    int ws_at(const POINT& cl) const;      // pill/chip index, -1 outside
    bool ws_add_hit(const POINT& cl) const;
    void switch_workspace(int ws_id);
    void move_tab_to_workspace(int global_index, int ws_id);
    void reload_workspaces_state();

    // ---- Kiwi Shields --------------------------------------------------
    void open_shields_menu(const POINT& screen);
    int shields_session_total() const;
    void on_shields(int browser_id, int blocked) override;

    int add_tab(const std::string& url, bool private_tab, int workspace = 0);
    void activate_index(int index);
    void start_self_test_watchdog();
    void close_index(int index);
    void close_active_tab();
    void close_others(int keep_index);
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
    void update_omnibox();
    void update_window_title();
    void set_status_text(const std::string& text);
    CefRefPtr<CefBrowser> active_browser();
    WinTab* tab_by_id(int browser_id);
    int index_by_id(int browser_id);
    std::string start_uri(bool private_tab);
    bool zen_style() const { return style_ == "zen"; }

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
    void on_open_url(int browser_id, const std::string& url, bool background) override;
    void on_favicon_urls(int browser_id, const std::vector<std::string>& urls) override;

    HWND hwnd_ = nullptr;
    HWND host_ = nullptr;
    HWND omnibox_ = nullptr;
    HWND find_entry_ = nullptr;
    HFONT font_ = nullptr;
    HICON icon_big_ = nullptr;
    HICON icon_small_ = nullptr;

    struct UiLayout {
        int dpi = 96;
        bool zen = false;
        RECT tab_area = {};
        RECT add_btn = {};
        RECT tool_btns[5] = {};
        RECT omnibox = {};
        RECT omnibox_pill = {};
        RECT min_btn = {};
        RECT max_btn = {};
        RECT close_btn = {};
        RECT rail = {};
        RECT status = {};
        RECT find_row = {};
        RECT find_entry = {};
        RECT find_prev = {};
        RECT find_next = {};
        RECT find_close = {};
        RECT find_label = {};
        RECT host = {};
        RECT devtools = {};
        RECT devtools_bar = {};
        RECT devtools_base = {};
        RECT shield_btn = {};  // inside the omnibox pill (Kiwi Shields)
    } lay_;

    int find_seq_ = 0;
    std::string find_label_text_;
    bool find_visible_ = false;

    std::string style_;
    std::string backdrop_;   // "off", "mica" or "acrylic" (style.toml)
    std::string rail_side_;  // "left" or "right" (style.toml)

    // Opera-style workspaces: defs from <config>/workspaces.toml, the active
    // one and the per-workspace memory of the last active tab (browser id).
    std::vector<WorkspaceDef> workspaces_;
    int active_ws_ = 1;
    std::map<int, int> ws_active_tab_;
    std::vector<RECT> ws_pills_;  // zen rail pill rects, index = ws list index
    RECT ws_add_pill_ = {};       // zen: the "add workspace" pill
    std::vector<RECT> ws_chips_;  // classic strip glyph chips
    int hover_ws_ = -1;

    bool star_active_ = false;
    std::string status_text_;

    bool hover_shield_ = false;
    bool press_shield_ = false;

    int spin_phase_ = 0;
    int hover_tab_ = -1;
    bool hover_close_ = false;
    int press_close_tab_ = -1;
    int hover_tool_ = -1;
    int press_tool_ = -1;
    bool hover_add_ = false;
    bool press_add_ = false;
    int hover_caption_ = -1;
    bool self_test_ = false;

    int drag_tab_ = -1;
    bool drag_active_ = false;
    bool drag_tear_ = false;
    POINT drag_start_ = {};
    POINT drag_pos_ = {};
    int grab_dx_ = 0;
    int grab_dy_ = 0;
    int drop_slot_ = -1;
    bool drag_thumb_ = false;
    POINT grab_pos_ = {};
    int grab_scroll_ = 0;
    bool drag_split_ = false;
    bool drag_rail_ = false;
    bool rail_flip_done_ = false;
    bool hover_rail_grip_ = false;
    int tab_scroll_ = 0;

    int devtools_dock_ = DOCK_BOTTOM;
    HWND tear_hint_ = nullptr;
    bool primary_ = false;
    POINT init_pos_ = {-1, -1};

    CefRefPtr<CefBrowser> devtools_browser_;
    int devtools_tab_id_ = 0;
    HWND devtools_hwnd_ = nullptr;
    int devtools_split_ = 0;

    std::vector<WinTab> tabs_;
    int active_ = -1;
    bool closing_ = false;
    CefRefPtr<CefRequestContext> private_context_;
    CefRefPtr<DevToolsClient> devtools_client_ = new DevToolsClient();
};

CefShell* g_shell = nullptr;

std::vector<CefShell*>& shell_windows() {
    static std::vector<CefShell*> windows;
    return windows;
}

int CefShell::exec() {
    trace_stage("shell: create_window begin");
    if (!create_window()) {
        trace_stage("shell: create_window FAILED");
        return 1;
    }
    trace_stage("shell: create_window ok, opening first tab");
    if (!restore_session()) {
        add_tab(start_uri(false), false);
        trace_stage("shell: first tab submitted");
        activate_index(0);
    } else {
        trace_stage("shell: session restored");
    }
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

bool CefShell::backdrop_active() const {
    return backdrop_ != "off" && dwm().ok() && windows_build() >= 22621;
}

void CefShell::apply_backdrop() {
    if (!dwm().ok() || !hwnd_) return;
    DWORD corner = kDwmcpRound;
    dwm().set_attr(hwnd_, kDwmwaWindowCornerPreference, &corner, sizeof(corner));
    DWORD dark = gfx::dark_chrome() ? 1 : 0;
    dwm().set_attr(hwnd_, kDwmwaUseImmersiveDarkMode, &dark, sizeof(dark));
    DWORD backdrop = kDwmsbtNone;
    if (backdrop_active()) {
        backdrop = backdrop_ == "acrylic" ? kDwmsbtAcrylic : kDwmsbtMica;
    }
    dwm().set_attr(hwnd_, kDwmwaSystemBackdropType, &backdrop, sizeof(backdrop));
    MARGINS margins = backdrop_active() ? MARGINS{-1, -1, -1, -1}
                                        : MARGINS{0, 0, 0, 0};
    dwm().extend_frame(hwnd_, &margins);
    InvalidateRect(hwnd_, nullptr, TRUE);
}

void CefShell::reload_style() {
    Runtime* rt = runtime();
    style_ = kiwi::load_shell_style(rt ? rt->config_dir : std::string());
    backdrop_ = kiwi::load_shell_backdrop(rt ? rt->config_dir : std::string());
    rail_side_ = kiwi::load_shell_rail_side(rt ? rt->config_dir : std::string());
    gfx::apply_named_theme(
        kiwi::load_shell_theme(rt ? rt->config_dir : std::string()),
        kiwi::load_shell_accent(rt ? rt->config_dir : std::string()));
    apply_backdrop();
    layout();
}

bool CefShell::create_window() {
    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = wnd_proc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.hCursor = LoadCursorW(nullptr, reinterpret_cast<LPCWSTR>(IDC_ARROW));
    wc.lpszClassName = kWindowClass;
    icon_big_ = app_icon(GetSystemMetrics(SM_CXICON));
    icon_small_ = app_icon(GetSystemMetrics(SM_CXSMICON));
    wc.hIcon = icon_big_;
    wc.hIconSm = icon_small_;
    static bool class_registered = false;
    if (!class_registered) {
        if (!RegisterClassExW(&wc)) return false;
        class_registered = true;
    }

    Runtime* rt = runtime();
    // The named chrome theme (style.toml) decides light/dark before the first
    // window exists, so the DWM caption and the first paint agree.
    gfx::apply_named_theme(
        kiwi::load_shell_theme(rt ? rt->config_dir : std::string()),
        kiwi::load_shell_accent(rt ? rt->config_dir : std::string()));
    int width = rt ? rt->settings.window_width : 1280;
    int height = rt ? rt->settings.window_height : 800;

    RECT area = {0, 0, width, height};
    AdjustWindowRect(&area, WS_OVERLAPPEDWINDOW, FALSE);
    int pos_x = init_pos_.x >= 0 ? init_pos_.x : CW_USEDEFAULT;
    int pos_y = init_pos_.y >= 0 ? init_pos_.y : CW_USEDEFAULT;
    init_pos_ = {-1, -1};
    hwnd_ = CreateWindowExW(0, kWindowClass, L"Kiwi", WS_OVERLAPPEDWINDOW,
        pos_x, pos_y, area.right - area.left, area.bottom - area.top,
        nullptr, nullptr, GetModuleHandleW(nullptr), this);
    if (!hwnd_) return false;
    shell_windows().push_back(this);

    font_ = CreateFontW(-MulDiv(10, dpi_for(hwnd_), 72), 0, 0, 0, FW_NORMAL, FALSE, FALSE,
        FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
        DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");

    style_ = kiwi::load_shell_style(rt ? rt->config_dir : std::string());
    backdrop_ = kiwi::load_shell_backdrop(rt ? rt->config_dir : std::string());
    rail_side_ = kiwi::load_shell_rail_side(rt ? rt->config_dir : std::string());
    std::string dock = kiwi::load_shell_devtools_dock(rt ? rt->config_dir : std::string());
    load_workspaces_state();
    devtools_dock_ = dock == "left" ? DOCK_LEFT
                   : dock == "right" ? DOCK_RIGHT : DOCK_BOTTOM;
    gfx::init();
    if (rt) favicons::set_profile_root(rt->profile_root);
    favicons::set_notifier([]() {
        for (CefShell* window : shell_windows()) {
            if (window->hwnd_) InvalidateRect(window->hwnd_, nullptr, FALSE);
        }
    });
    devtools_client_->set_on_created([this](CefRefPtr<CefBrowser> browser) {
        devtools_browser_ = browser;
        devtools_hwnd_ = browser->GetHost()->GetWindowHandle();
        layout();
    });
    devtools_client_->set_on_closed([this](CefRefPtr<CefBrowser>) {
        devtools_browser_ = nullptr;
        devtools_hwnd_ = nullptr;
        devtools_tab_id_ = 0;
        if (!closing_) layout();
    });

    create_controls();
    apply_backdrop();
    ShowWindow(hwnd_, SW_SHOW);
    UpdateWindow(hwnd_);
    return true;
}

void CefShell::open_in_new_tab(const std::string& url) {
    if (url.empty()) return;
    int index = add_tab(url, false);
    activate_index(index);
    ShowWindow(hwnd_, SW_RESTORE);
    SetForegroundWindow(hwnd_);
}

void CefShell::create_controls() {
    omnibox_ = CreateWindowExW(0, L"EDIT", nullptr,
        WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL | ES_WANTRETURN,
        0, 0, 10, 10, hwnd_, reinterpret_cast<HMENU>(static_cast<INT_PTR>(104)),
        GetModuleHandleW(nullptr), nullptr);
    SetWindowSubclass(omnibox_, omnibox_proc, 1, reinterpret_cast<DWORD_PTR>(this));
    SendMessageW(omnibox_, EM_SETCUEBANNER, TRUE,
        reinterpret_cast<LPARAM>(L"Search or enter address"));
    SendMessageW(omnibox_, WM_SETFONT, (WPARAM)font_, TRUE);

    host_ = CreateWindowExW(0, L"STATIC", nullptr,
        WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | WS_CLIPCHILDREN,
        0, 0, 10, 10, hwnd_, reinterpret_cast<HMENU>(static_cast<INT_PTR>(105)),
        GetModuleHandleW(nullptr), nullptr);

    find_entry_ = CreateWindowExW(0, L"EDIT", nullptr,
        WS_CHILD | ES_AUTOHSCROLL | ES_WANTRETURN,
        0, 0, 10, 10, hwnd_, reinterpret_cast<HMENU>(static_cast<INT_PTR>(106)),
        GetModuleHandleW(nullptr), nullptr);
    SetWindowSubclass(find_entry_, find_entry_proc, 1, reinterpret_cast<DWORD_PTR>(this));
    SendMessageW(find_entry_, EM_SETCUEBANNER, TRUE,
        reinterpret_cast<LPARAM>(L"Find in page"));
    SendMessageW(find_entry_, WM_SETFONT, (WPARAM)font_, TRUE);
}

void CefShell::set_status_text(const std::string& text) {
    bool was = status_text_.empty();
    status_text_ = text;
    if (was != text.empty()) {
        layout();
    } else if (!status_text_.empty()) {
        InvalidateRect(hwnd_, &lay_.status, FALSE);
    }
}

void CefShell::sync_spin_timer() {
    bool loading = false;
    for (const WinTab& tab : tabs_) {
        if (tab.loading) {
            loading = true;
            break;
        }
    }
    if (loading && !self_test_) {
        SetTimer(hwnd_, kSpinTimer, 66, nullptr);
    } else {
        KillTimer(hwnd_, kSpinTimer);
    }
}

void CefShell::set_tab_loading(int browser_id, bool loading) {
    WinTab* tab = tab_by_id(browser_id);
    if (!tab || tab->loading == loading) return;
    tab->loading = loading;
    sync_spin_timer();
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void CefShell::update_favicon_host(WinTab& tab) {
    if (tab.private_tab || tab.url.rfind("http://", 0) != 0
        && tab.url.rfind("https://", 0) != 0) {
        tab.favicon_host.clear();
        return;
    }
    tab.favicon_host = url_host(tab.url);
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
        case WM_ERASEBKGND:
            return 1;
        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC dc = BeginPaint(hwnd, &ps);
            RECT client;
            GetClientRect(hwnd, &client);
            self->paint(dc, client);
            EndPaint(hwnd, &ps);
            return 0;
        }
        case WM_CTLCOLOREDIT: {
            HDC dc = reinterpret_cast<HDC>(wparam);
            const gfx::Palette& pal = gfx::palette();
            SetBkColor(dc, pal.omnibox_bg);
            SetTextColor(dc, pal.chrome_text);
            static HBRUSH light_omni = CreateSolidBrush(RGB(255, 255, 255));
            static HBRUSH dark_omni = CreateSolidBrush(RGB(42, 45, 49));
            return reinterpret_cast<LRESULT>(gfx::dark_chrome() ? dark_omni
                                                                : light_omni);
        }
        case WM_CTLCOLORSTATIC: {
            HDC dc = reinterpret_cast<HDC>(wparam);
            const gfx::Palette& pal = gfx::palette();
            SetBkColor(dc, pal.chrome_bg);
            SetTextColor(dc, pal.chrome_text);
            static HBRUSH light_chrome = CreateSolidBrush(RGB(243, 243, 246));
            static HBRUSH dark_chrome_br = CreateSolidBrush(RGB(32, 33, 36));
            return reinterpret_cast<LRESULT>(gfx::dark_chrome() ? dark_chrome_br
                                                                : light_chrome);
        }
        case WM_COMMAND:
            self->on_command(wparam, lparam);
            return 0;
        case WM_MOUSEMOVE: {
            TRACKMOUSEEVENT track = {sizeof(track), TME_LEAVE, hwnd, 0};
            TrackMouseEvent(&track);
            POINT cl = {GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam)};
            if (self->drag_split_) {
                if (self->devtools_dock_ == DOCK_LEFT) {
                    self->devtools_split_ = cl.x - self->lay_.devtools_base.left;
                } else if (self->devtools_dock_ == DOCK_RIGHT) {
                    self->devtools_split_ = self->lay_.devtools_base.right - cl.x;
                } else {
                    self->devtools_split_ = self->lay_.devtools_base.bottom - cl.y;
                }
                self->layout();
                return 0;
            }
            if (self->drag_thumb_) {
                int maxs = self->max_tab_scroll();
                if (maxs > 0) {
                    RECT thumb = self->tab_thumb_rect();
                    int span = self->lay_.zen ? thumb.bottom - thumb.top
                                              : thumb.right - thumb.left;
                    int delta = self->lay_.zen ? cl.y - self->grab_pos_.y
                                               : cl.x - self->grab_pos_.x;
                    if (span > 0) {
                        self->tab_scroll_ = self->grab_scroll_
                            + MulDiv(delta, maxs, span);
                        self->clamp_tab_scroll();
                    }
                }
                InvalidateRect(hwnd, nullptr, FALSE);
                return 0;
            }
            if (self->drag_tab_ >= 0) {
                if (!self->drag_active_) {
                    int dx = cl.x - self->drag_start_.x;
                    int dy = cl.y - self->drag_start_.y;
                    if (dx < 0) dx = -dx;
                    if (dy < 0) dy = -dy;
                    int threshold = MulDiv(6, self->lay_.dpi, 96);
                    if ((self->lay_.zen ? dy : dx) > threshold) {
                        self->drag_active_ = true;
                    }
                }
                if (self->drag_active_) {
                    self->drag_pos_ = cl;
                    bool tear = self->tear_zone_at(cl);
                    if (tear != self->drag_tear_) {
                        self->drag_tear_ = tear;
                        self->drop_slot_ = tear ? -1 : self->drop_slot_at(cl);
                        if (tear) self->show_tear_hint();
                        else self->hide_tear_hint();
                        InvalidateRect(hwnd, nullptr, FALSE);
                    } else if (tear) {
                        self->move_tear_hint(cl);
                    } else {
                        self->drop_slot_ = self->drop_slot_at(cl);
                        InvalidateRect(hwnd, nullptr, FALSE);
                    }
                }
                return 0;
            }
            if (self->drag_rail_) {
                int dx = cl.x - self->grab_pos_.x;
                int threshold = MulDiv(56, self->lay_.dpi, 96);
                if (!self->rail_flip_done_) {
                    if (self->rail_side_ == "left" && dx > threshold) {
                        self->flip_rail_side("right");
                        self->rail_flip_done_ = true;
                    } else if (self->rail_side_ == "right" && dx < -threshold) {
                        self->flip_rail_side("left");
                        self->rail_flip_done_ = true;
                    }
                }
                return 0;
            }
            int tab = self->tab_at(cl);
            bool close = false;
            if (tab >= 0) {
                int slot = self->slot_of_tab(tab);
                RECT chip = self->tab_chip_rect(slot, self->tab_width_for(
                    self->ws_tab_count()));
                RECT crect = self->tab_close_rect(chip);
                close = PtInRect(&crect, cl) != FALSE;
            }
            int tool = self->tool_at(cl);
            const RECT& add = self->lay_.add_btn;
            bool add_hover = PtInRect(&add, cl) != FALSE;
            bool shield_hover = PtInRect(&self->lay_.shield_btn, cl) != FALSE;
            int ws_hover = self->ws_at(cl);
            bool grip_hover = false;
            if (self->lay_.zen) {
                RECT grip = self->rail_grip_rect();
                grip_hover = PtInRect(&grip, cl) != FALSE;
            }
            if (tab != self->hover_tab_ || close != self->hover_close_
                || tool != self->hover_tool_ || add_hover != self->hover_add_
                || shield_hover != self->hover_shield_
                || ws_hover != self->hover_ws_
                || grip_hover != self->hover_rail_grip_) {
                self->hover_tab_ = tab;
                self->hover_close_ = close;
                self->hover_tool_ = tool;
                self->hover_add_ = add_hover;
                self->hover_shield_ = shield_hover;
                self->hover_ws_ = ws_hover;
                self->hover_rail_grip_ = grip_hover;
                InvalidateRect(hwnd, nullptr, FALSE);
            }
            return 0;
        }
        case WM_MOUSELEAVE:
            if (self->hover_tab_ != -1 || self->hover_close_ || self->hover_tool_ != -1
                || self->hover_add_ || self->hover_rail_grip_
                || self->hover_shield_ || self->hover_ws_ != -1) {
                self->hover_tab_ = -1;
                self->hover_close_ = false;
                self->hover_tool_ = -1;
                self->hover_add_ = false;
                self->hover_rail_grip_ = false;
                self->hover_shield_ = false;
                self->hover_ws_ = -1;
                InvalidateRect(hwnd, nullptr, FALSE);
            }
            return 0;
        case WM_NCMOUSEMOVE: {
            TRACKMOUSEEVENT track = {sizeof(track), TME_NONCLIENT | TME_LEAVE, hwnd, 0};
            TrackMouseEvent(&track);
            POINT pt = {GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam)};
            POINT cl = pt;
            ScreenToClient(hwnd, &cl);
            int cap = -1;
            if (PtInRect(&self->lay_.min_btn, cl)) cap = 0;
            if (PtInRect(&self->lay_.max_btn, cl)) cap = 1;
            if (PtInRect(&self->lay_.close_btn, cl)) cap = 2;
            if (cap != self->hover_caption_) {
                self->hover_caption_ = cap;
                InvalidateRect(hwnd, nullptr, FALSE);
            }
            return 0;
        }
        case WM_NCMOUSELEAVE:
            if (self->hover_caption_ != -1) {
                self->hover_caption_ = -1;
                InvalidateRect(hwnd, nullptr, FALSE);
            }
            return 0;
        case WM_NCCALCSIZE: {
            if (!wparam) return DefWindowProcW(hwnd, msg, wparam, lparam);
            auto* params = reinterpret_cast<NCCALCSIZE_PARAMS*>(lparam);
            if (IsZoomed(hwnd)) {
                int frame = GetSystemMetrics(SM_CXFRAME)
                    + GetSystemMetrics(SM_CXPADDEDBORDER);
                params->rgrc[0].left += frame;
                params->rgrc[0].top += frame;
                params->rgrc[0].right -= frame;
                params->rgrc[0].bottom -= frame;
            }
            return 0;
        }
        case WM_NCHITTEST: {
            POINT pt = {GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam)};
            RECT wr;
            GetWindowRect(hwnd, &wr);
            int rx = pt.x - wr.left;
            int ry = pt.y - wr.top;
            int ww = wr.right - wr.left;
            int wh = wr.bottom - wr.top;
            if (!IsZoomed(hwnd)) {
                int b = GetSystemMetrics(SM_CXFRAME) + GetSystemMetrics(SM_CXPADDEDBORDER);
                bool left = rx < b;
                bool right = rx >= ww - b;
                bool top = ry < b;
                bool bottom = ry >= wh - b;
                if (top && left) return HTTOPLEFT;
                if (top && right) return HTTOPRIGHT;
                if (bottom && left) return HTBOTTOMLEFT;
                if (bottom && right) return HTBOTTOMRIGHT;
                if (left) return HTLEFT;
                if (right) return HTRIGHT;
                if (top) return HTTOP;
                if (bottom) return HTBOTTOM;
            }
            POINT cl = pt;
            ScreenToClient(hwnd, &cl);
            if (PtInRect(&self->lay_.min_btn, cl)) return HTMINBUTTON;
            if (PtInRect(&self->lay_.max_btn, cl)) return HTMAXBUTTON;
            if (PtInRect(&self->lay_.close_btn, cl)) return HTCLOSE;
            if (self->chrome_is_drag_zone(cl)) return HTCAPTION;
            return HTCLIENT;
        }
        case WM_LBUTTONDOWN: {
            POINT cl = {GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam)};
            if (self->devtools_bar_hit(cl)) {
                self->drag_split_ = true;
                SetCapture(hwnd);
                return 0;
            }
            RECT thumb = self->tab_thumb_rect();
            if (!IsRectEmpty(&thumb) && PtInRect(&thumb, cl)) {
                self->drag_thumb_ = true;
                self->grab_pos_ = cl;
                self->grab_scroll_ = self->tab_scroll_;
                SetCapture(hwnd);
                return 0;
            }
            if (self->lay_.zen) {
                RECT grip = self->rail_grip_rect();
                if (PtInRect(&grip, cl)) {
                    self->drag_rail_ = true;
                    self->rail_flip_done_ = false;
                    self->grab_pos_ = cl;
                    SetCapture(hwnd);
                    return 0;
                }
            }
            // Workspace pills/chips switch on press; the "+" pill and a
            // right-click open the manager.
            int ws = self->ws_at(cl);
            if (ws >= 0) {
                if (ws < static_cast<int>(self->workspaces_.size())) {
                    self->switch_workspace(self->workspaces_[static_cast<size_t>(ws)].id);
                } else {
                    self->open_workspaces_window(self->hwnd_);
                }
                return 0;
            }
            if (PtInRect(&self->lay_.shield_btn, cl)) {
                self->press_shield_ = true;
                SetCapture(hwnd);
                InvalidateRect(hwnd, nullptr, FALSE);
                return 0;
            }
            int tab = self->tab_at(cl);
            if (tab >= 0) {
                int slot = self->slot_of_tab(tab);
                RECT chip = self->tab_chip_rect(slot, self->tab_width_for(
                    self->ws_tab_count()));
                RECT crect = self->tab_close_rect(chip);
                if (PtInRect(&crect, cl)) {
                    self->press_close_tab_ = tab;
                    SetCapture(hwnd);
                    InvalidateRect(hwnd, nullptr, FALSE);
                    return 0;
                }
                self->drag_tab_ = tab;
                self->drag_active_ = false;
                self->drop_slot_ = tab;
                self->drag_start_ = cl;
                if (self->lay_.zen) {
                    self->grab_dy_ = cl.y - chip.top;
                    self->grab_dx_ = (chip.right - chip.left) / 2;
                } else {
                    self->grab_dx_ = cl.x - chip.left;
                    self->grab_dy_ = (chip.bottom - chip.top) / 2;
                }
                SetCapture(hwnd);
                self->activate_index(tab);
                return 0;
            }
            if (PtInRect(&self->lay_.add_btn, cl)) {
                self->press_add_ = true;
                SetCapture(hwnd);
                InvalidateRect(hwnd, nullptr, FALSE);
                return 0;
            }
            int tool = self->tool_at(cl);
            if (tool >= 0) {
                self->press_tool_ = tool;
                SetCapture(hwnd);
                InvalidateRect(hwnd, nullptr, FALSE);
                return 0;
            }
            if (self->find_visible_) {
                if (PtInRect(&self->lay_.find_prev, cl)) {
                    self->do_find(false, true);
                    return 0;
                }
                if (PtInRect(&self->lay_.find_next, cl)) {
                    self->do_find(true, true);
                    return 0;
                }
                if (PtInRect(&self->lay_.find_close, cl)) {
                    self->close_find_bar();
                    return 0;
                }
            }
            break;
        }
        case WM_LBUTTONUP: {
            POINT cl = {GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam)};
            // Snapshot the press/drag state first: ReleaseCapture delivers
            // WM_CAPTURECHANGED synchronously and that handler resets all of
            // the flags below before the commit code gets to read them.
            const bool had_split = self->drag_split_;
            const bool had_thumb = self->drag_thumb_;
            const bool had_rail = self->drag_rail_;
            const int drag_from = self->drag_tab_;
            const bool drag_was_active = self->drag_active_;
            const bool drag_was_tear = self->drag_tear_;
            const int drag_to = self->drop_slot_;
            const int close_tab = self->press_close_tab_;
            const bool add_press = self->press_add_;
            const int tool = self->press_tool_;
            const bool shield_press = self->press_shield_;
            if (GetCapture() == hwnd) ReleaseCapture();
            self->drag_split_ = false;
            self->drag_thumb_ = false;
            self->drag_rail_ = false;
            self->rail_flip_done_ = false;
            self->drag_tab_ = -1;
            self->drag_active_ = false;
            self->drag_tear_ = false;
            self->drop_slot_ = -1;
            self->press_close_tab_ = -1;
            self->press_add_ = false;
            self->press_tool_ = -1;
            self->press_shield_ = false;
            if (had_split) return 0;
            if (had_thumb) return 0;
            if (had_rail) return 0;
            if (drag_from >= 0) {
                if (drag_was_active && drag_was_tear) {
                    self->hide_tear_hint();
                    POINT screen = cl;
                    ClientToScreen(hwnd, &screen);
                    self->tear_off_window(drag_from, screen);
                } else if (drag_was_active && drag_to >= 0 && drag_to != drag_from) {
                    self->move_tab(drag_from, drag_to);
                }
                InvalidateRect(hwnd, nullptr, FALSE);
                return 0;
            }
            if (close_tab >= 0) {
                int cslot = self->slot_of_tab(close_tab);
                if (cslot >= 0) {
                    RECT chip = self->tab_chip_rect(cslot, self->tab_width_for(
                        self->ws_tab_count()));
                    RECT crect = self->tab_close_rect(chip);
                    if (cl.x >= chip.left && cl.x <= chip.right
                        && cl.y >= chip.top && cl.y <= chip.bottom
                        && PtInRect(&crect, cl)) {
                        self->close_index(close_tab);
                    }
                }
                InvalidateRect(hwnd, nullptr, FALSE);
                return 0;
            }
            if (add_press) {
                if (PtInRect(&self->lay_.add_btn, cl)) self->on_hotkey(HK_NEW_TAB);
                InvalidateRect(hwnd, nullptr, FALSE);
                return 0;
            }
            if (tool >= 0) {
                if (tool < 5 && PtInRect(&self->lay_.tool_btns[tool], cl)) {
                    switch (tool) {
                        case 0:
                            if (CefRefPtr<CefBrowser> b = self->active_browser()) b->GoBack();
                            break;
                        case 1:
                            if (CefRefPtr<CefBrowser> b = self->active_browser()) b->GoForward();
                            break;
                        case 2:
                            self->on_hotkey(HK_RELOAD);
                            break;
                        case 3:
                            self->toggle_bookmark();
                            break;
                        case 4:
                            self->on_hotkey(HK_SETTINGS);
                            break;
                        default:
                            break;
                    }
                }
                InvalidateRect(hwnd, nullptr, FALSE);
                return 0;
            }
            if (shield_press) {
                if (PtInRect(&self->lay_.shield_btn, cl)) {
                    POINT screen = cl;
                    ClientToScreen(hwnd, &screen);
                    self->open_shields_menu(screen);
                }
                InvalidateRect(hwnd, nullptr, FALSE);
                return 0;
            }
            break;
        }
        case WM_CAPTURECHANGED:
            self->press_close_tab_ = -1;
            self->press_tool_ = -1;
            self->press_add_ = false;
            self->press_shield_ = false;
            self->drag_tab_ = -1;
            self->drag_active_ = false;
            self->drag_tear_ = false;
            self->drop_slot_ = -1;
            self->drag_thumb_ = false;
            self->drag_split_ = false;
            self->drag_rail_ = false;
            self->rail_flip_done_ = false;
            self->hide_tear_hint();
            return 0;
        case WM_MBUTTONUP: {
            POINT cl = {GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam)};
            int tab = self->tab_at(cl);
            if (tab >= 0) self->close_index(tab);
            return 0;
        }
        case WM_RBUTTONUP: {
            POINT cl = {GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam)};
            POINT screen = cl;
            ClientToScreen(hwnd, &screen);
            if (self->devtools_bar_hit(cl)) {
                self->open_devtools_dock_menu(screen);
                return 0;
            }
            if (self->ws_at(cl) >= 0 || self->ws_add_hit(cl)) {
                self->open_workspaces_window(self->hwnd_);
                return 0;
            }
            int tab = self->tab_at(cl);
            self->open_tab_context_menu(tab, screen);
            return 0;
        }
        case WM_TIMER:
            if (wparam == kSpinTimer) {
                self->spin_phase_ = (self->spin_phase_ + 1) % 16;
                InvalidateRect(hwnd, nullptr, FALSE);
            }
            return 0;
        case WM_MOUSEWHEEL: {
            POINT pt = {GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam)};
            ScreenToClient(hwnd, &pt);
            if (PtInRect(&self->lay_.tab_area, pt) && self->max_tab_scroll() > 0) {
                int delta = static_cast<short>(GET_WHEEL_DELTA_WPARAM(wparam));
                self->scroll_tabs(-delta);
                return 0;
            }
            break;
        }
        case WM_SETCURSOR: {
            if (LOWORD(lparam) == HTCLIENT) {
                POINT pt;
                GetCursorPos(&pt);
                ScreenToClient(hwnd, &pt);
                if (self->devtools_bar_hit(pt)) {
                    SetCursor(LoadCursorW(nullptr,
                        self->devtools_dock_ == DOCK_BOTTOM ? IDC_SIZENS : IDC_SIZEWE));
                    return TRUE;
                }
                RECT grip = self->rail_grip_rect();
                if (self->lay_.zen && PtInRect(&grip, pt)) {
                    SetCursor(LoadCursorW(nullptr, IDC_SIZEWE));
                    return TRUE;
                }
            }
            break;
        }
        case WM_GETMINMAXINFO: {
            auto* info = reinterpret_cast<MINMAXINFO*>(lparam);
            info->ptMinTrackSize.x = scale_for(hwnd, 560);
            info->ptMinTrackSize.y = scale_for(hwnd, 420);
            return 0;
        }
        case WM_DPICHANGED: {
            if (self->font_) DeleteObject(self->font_);
            self->font_ = CreateFontW(-MulDiv(10, HIWORD(wparam), 72), 0, 0, 0,
                FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE,
                L"Segoe UI");
            SendMessageW(self->omnibox_, WM_SETFONT, (WPARAM)self->font_, TRUE);
            SendMessageW(self->find_entry_, WM_SETFONT, (WPARAM)self->font_, TRUE);
            auto* suggested = reinterpret_cast<RECT*>(lparam);
            SetWindowPos(hwnd, nullptr, suggested->left, suggested->top,
                suggested->right - suggested->left, suggested->bottom - suggested->top,
                SWP_NOZORDER | SWP_NOACTIVATE);
            self->layout();
            return 0;
        }
        case WM_CLOSE:
            self->store_geometry();
            self->save_session_now();
            self->closing_ = true;
            for (WinTab& tab : self->tabs_) {
                if (tab.browser) tab.browser->GetHost()->CloseBrowser(false);
            }
            if (self->tabs_.empty()) DestroyWindow(hwnd);
            return 0;
        case WM_APP_SYNC_WS:
            self->reload_workspaces_state();
            return 0;
        case WM_APP_SELF_TEST:
            g_self_test_exit = self->active_browser() ? 0 : 2;
            DestroyWindow(hwnd);
            return 0;
        case WM_DESTROY:
            KillTimer(hwnd, kSpinTimer);
            self->hide_tear_hint();
            if (self->tear_hint_) DestroyWindow(self->tear_hint_);
            self->tear_hint_ = nullptr;
            {
                auto& windows = shell_windows();
                windows.erase(std::remove(windows.begin(), windows.end(), self),
                    windows.end());
                if (windows.empty()) CefQuitMessageLoop();
            }
            return 0;
        default:
            return DefWindowProcW(hwnd, msg, wparam, lparam);
    }
    return DefWindowProcW(hwnd, msg, wparam, lparam);
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
    if (msg == WM_SETFOCUS || msg == WM_KILLFOCUS) {
        if (self->hwnd_) InvalidateRect(self->hwnd_, &self->lay_.omnibox_pill, FALSE);
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

LRESULT CALLBACK CefShell::browser_wheel_proc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam,
                                              UINT_PTR id, DWORD_PTR data) {
    (void)id;
    auto* self = reinterpret_cast<CefShell*>(data);
    if (msg == WM_MOUSEWHEEL) {
        POINT pt = {GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam)};
        ScreenToClient(self->hwnd_, &pt);
        if (self->max_tab_scroll() > 0 && PtInRect(&self->lay_.tab_area, pt)) {
            return SendMessageW(self->hwnd_, WM_MOUSEWHEEL, wparam, lparam);
        }
    }
    return DefSubclassProc(hwnd, msg, wparam, lparam);
}

void CefShell::on_command(WPARAM wparam, LPARAM lparam) {
    (void)lparam;
    switch (LOWORD(wparam)) {
        case 106:
            if (HIWORD(wparam) == EN_CHANGE) on_find_entry_changed();
            break;
        default:
            break;
    }
}

void CefShell::layout() {
    if (!hwnd_ || !host_) return;
    RECT client;
    GetClientRect(hwnd_, &client);
    int w = client.right - client.left;
    int h = client.bottom - client.top;
    int dpi = dpi_for(hwnd_);
    lay_ = UiLayout();
    lay_.dpi = dpi;
    lay_.zen = zen_style();

    int status_h = status_text_.empty() ? 0 : MulDiv(24, dpi, 96);
    int find_h = find_visible_ ? MulDiv(40, dpi, 96) : 0;
    int top_h = MulDiv(lay_.zen ? 46 : 40, dpi, 96);
    int tool_h = MulDiv(44, dpi, 96);
    int btn = MulDiv(34, dpi, 96);
    int pad = MulDiv(6, dpi, 96);
    int cap_w = MulDiv(44, dpi, 96);
    int cap_zone = cap_w * 3;

    lay_.min_btn = {w - cap_zone, 0, w - cap_w * 2, top_h};
    lay_.max_btn = {w - cap_w * 2, 0, w - cap_w, top_h};
    lay_.close_btn = {w - cap_w, 0, w, top_h};

    int omni_h = MulDiv(30, dpi, 96);
    int top = top_h + find_h;

    if (lay_.zen) {
        int x = pad;
        int y = (top_h - btn) / 2;
        for (int i = 0; i < 3; ++i) {
            lay_.tool_btns[i] = {x, y, x + btn, y + btn};
            x += btn + pad / 2;
        }
        int gear_x = w - cap_zone - pad - btn;
        int star_x = gear_x - pad - btn;
        lay_.tool_btns[4] = {gear_x, y, gear_x + btn, y + btn};
        lay_.tool_btns[3] = {star_x, y, star_x + btn, y + btn};
        int omni_left = x + pad;
        int omni_right = star_x - pad;
        if (omni_right - omni_left < MulDiv(80, dpi, 96)) {
            omni_right = omni_left + MulDiv(80, dpi, 96);
        }
        lay_.omnibox_pill = {omni_left, (top_h - omni_h) / 2, omni_right,
                             (top_h + omni_h) / 2};
        int rail_w = MulDiv(230, dpi, 96);
        if (rail_w > w / 2) rail_w = w / 2;
        bool rail_right = rail_side_ == "right";
        if (rail_right) {
            lay_.rail = {w - rail_w, top, w, h - status_h};
        } else {
            lay_.rail = {0, top, rail_w, h - status_h};
        }
        // Opera-style workspace section: one pill per workspace, then the
        // "+" pill (opens the manager), then the new-tab button and chips.
        ws_pills_.clear();
        int ws_y = lay_.rail.top + MulDiv(6, dpi, 96);
        int pill_h = MulDiv(26, dpi, 96);
        int ws_step = MulDiv(30, dpi, 96);
        for (const WorkspaceDef& def : workspaces_) {
            (void)def;
            ws_pills_.push_back({lay_.rail.left + MulDiv(6, dpi, 96), ws_y,
                                 lay_.rail.right - MulDiv(6, dpi, 96),
                                 ws_y + pill_h});
            ws_y += ws_step;
        }
        ws_add_pill_ = {lay_.rail.left + MulDiv(6, dpi, 96), ws_y,
                        lay_.rail.right - MulDiv(6, dpi, 96),
                        ws_y + MulDiv(20, dpi, 96)};
        lay_.add_btn = add_btn_rect();
        lay_.tab_area = {lay_.rail.left, lay_.add_btn.bottom + MulDiv(2, dpi, 96),
                         lay_.rail.right, h - status_h};
        int host_left = rail_right ? 0 : rail_w;
        int host_right = rail_right ? w - rail_w : w;
        lay_.host = {host_left, top, host_right, h - status_h};
    } else {
        // Classic: workspace glyph chips lead the caption-row strip, then
        // the tab chips (only the active workspace's tabs are drawn).
        ws_chips_.clear();
        int chip = MulDiv(22, dpi, 96);
        int cx = pad;
        int cy = (top_h - chip) / 2;
        for (const WorkspaceDef& def : workspaces_) {
            (void)def;
            ws_chips_.push_back({cx, cy, cx + chip, cy + chip});
            cx += chip + MulDiv(4, dpi, 96);
        }
        if (!ws_chips_.empty()) cx += MulDiv(4, dpi, 96);
        lay_.tab_area = {cx, 0, w - cap_zone - MulDiv(4, dpi, 96), top_h};
        int x = pad;
        int y = top_h + (tool_h - btn) / 2;
        for (int i = 0; i < 3; ++i) {
            lay_.tool_btns[i] = {x, y, x + btn, y + btn};
            x += btn + pad / 2;
        }
        int gear_x = w - pad - btn;
        int star_x = gear_x - pad - btn;
        lay_.tool_btns[4] = {gear_x, y, gear_x + btn, y + btn};
        lay_.tool_btns[3] = {star_x, y, star_x + btn, y + btn};
        int omni_left = x + pad;
        int omni_right = star_x - pad;
        if (omni_right - omni_left < MulDiv(80, dpi, 96)) {
            omni_right = omni_left + MulDiv(80, dpi, 96);
        }
        lay_.omnibox_pill = {omni_left, top_h + (tool_h - omni_h) / 2, omni_right,
                             top_h + (tool_h + omni_h) / 2};
        lay_.host = {0, top_h + tool_h + find_h, w, h - status_h};
    }

    lay_.omnibox = {lay_.omnibox_pill.left + MulDiv(12, dpi, 96),
                    lay_.omnibox_pill.top + MulDiv(3, dpi, 96),
                    lay_.omnibox_pill.right - MulDiv(38, dpi, 96),
                    lay_.omnibox_pill.bottom - MulDiv(3, dpi, 96)};
    // Kiwi Shields button inside the pill's right end (Brave-style).
    int sb = MulDiv(22, dpi, 96);
    lay_.shield_btn = {lay_.omnibox_pill.right - MulDiv(30, dpi, 96),
                       (lay_.omnibox_pill.top + lay_.omnibox_pill.bottom - sb) / 2,
                       lay_.omnibox_pill.right - MulDiv(8, dpi, 96),
                       (lay_.omnibox_pill.top + lay_.omnibox_pill.bottom + sb) / 2};
    lay_.add_btn = add_btn_rect();

    if (find_visible_) {
        int fy = lay_.zen ? top_h : top_h + tool_h;
        lay_.find_row = {0, fy, w, fy + find_h};
        int entry_w = MulDiv(280, dpi, 96);
        int label_w = MulDiv(90, dpi, 96);
        int mini = MulDiv(28, dpi, 96);
        int gap = MulDiv(6, dpi, 96);
        int fx = pad;
        int by = fy + (find_h - mini) / 2;
        lay_.find_entry = {fx, fy + (find_h - omni_h) / 2 + MulDiv(2, dpi, 96),
                           fx + entry_w, fy + (find_h + omni_h) / 2 - MulDiv(2, dpi, 96)};
        lay_.find_label = {fx + entry_w + gap, fy, fx + entry_w + gap + label_w,
                           fy + find_h};
        int x_close = w - pad - mini;
        int x_next = x_close - gap - mini;
        int x_prev = x_next - gap - mini;
        lay_.find_close = {x_close, by, x_close + mini, by + mini};
        lay_.find_next = {x_next, by, x_next + mini, by + mini};
        lay_.find_prev = {x_prev, by, x_prev + mini, by + mini};
    }

    lay_.status = {0, h - status_h, w, h};
    lay_.devtools_base = lay_.host;
    if (devtools_docked_visible()) {
        int split = devtools_split_;
        if (devtools_dock_ == DOCK_BOTTOM) {
            int max_split = (lay_.host.bottom - lay_.host.top) - MulDiv(160, dpi, 96);
            int min_split = MulDiv(96, dpi, 96);
            if (split < min_split) split = min_split;
            if (split > max_split) split = max_split;
            if (split < min_split) split = min_split;
            devtools_split_ = split;
            lay_.devtools_bar = {lay_.host.left, lay_.host.bottom - split, lay_.host.right,
                                 lay_.host.bottom - split + MulDiv(4, dpi, 96)};
            lay_.devtools = {lay_.host.left, lay_.devtools_bar.bottom, lay_.host.right,
                             lay_.host.bottom};
            lay_.host.bottom = lay_.devtools_bar.top;
        } else {
            int min_split = MulDiv(180, dpi, 96);
            int avail = lay_.host.right - lay_.host.left;
            int max_split = avail - MulDiv(320, dpi, 96);
            if (max_split < min_split) max_split = avail > 0 ? avail : min_split;
            if (split < min_split) split = min_split;
            if (split > max_split) split = max_split;
            devtools_split_ = split;
            int bar_w = MulDiv(4, dpi, 96);
            if (devtools_dock_ == DOCK_LEFT) {
                lay_.devtools = {lay_.host.left, lay_.host.top, lay_.host.left + split,
                                 lay_.host.bottom};
                lay_.devtools_bar = {lay_.devtools.right, lay_.host.top,
                                     lay_.devtools.right + bar_w, lay_.host.bottom};
                lay_.host.left = lay_.devtools_bar.right;
            } else {
                lay_.devtools = {lay_.host.right - split, lay_.host.top, lay_.host.right,
                                 lay_.host.bottom};
                lay_.devtools_bar = {lay_.devtools.left - bar_w, lay_.host.top,
                                     lay_.devtools.left, lay_.host.bottom};
                lay_.host.right = lay_.devtools_bar.left;
            }
        }
        if (lay_.host.right < lay_.host.left) lay_.host.right = lay_.host.left;
        if (lay_.host.bottom < lay_.host.top) lay_.host.bottom = lay_.host.top;
    }
    clamp_tab_scroll();
    MoveWindow(host_, lay_.host.left, lay_.host.top,
        lay_.host.right - lay_.host.left, lay_.host.bottom - lay_.host.top, TRUE);
    MoveWindow(omnibox_, lay_.omnibox.left, lay_.omnibox.top,
        lay_.omnibox.right - lay_.omnibox.left,
        lay_.omnibox.bottom - lay_.omnibox.top, TRUE);
    if (devtools_hwnd_) {
        if (devtools_docked_visible()) {
            MoveWindow(devtools_hwnd_, lay_.devtools.left, lay_.devtools.top,
                lay_.devtools.right - lay_.devtools.left,
                lay_.devtools.bottom - lay_.devtools.top, TRUE);
            ShowWindow(devtools_hwnd_, SW_SHOW);
        } else {
            ShowWindow(devtools_hwnd_, SW_HIDE);
        }
    }
    if (find_visible_) {
        MoveWindow(find_entry_, lay_.find_entry.left, lay_.find_entry.top,
            lay_.find_entry.right - lay_.find_entry.left,
            lay_.find_entry.bottom - lay_.find_entry.top, TRUE);
        ShowWindow(find_entry_, SW_SHOW);
    } else {
        ShowWindow(find_entry_, SW_HIDE);
    }

    for (int i = 0; i < static_cast<int>(tabs_.size()); ++i) {
        if (!tabs_[i].browser) continue;
        HWND browser_hwnd = tabs_[i].browser->GetHost()->GetWindowHandle();
        if (!tabs_[i].wheel_subclassed) {
            SetWindowSubclass(browser_hwnd, browser_wheel_proc, 2,
                reinterpret_cast<DWORD_PTR>(this));
            tabs_[i].wheel_subclassed = true;
        }
        if (i == active_) {
            MoveWindow(browser_hwnd, 0, 0,
                lay_.host.right - lay_.host.left > 0 ? lay_.host.right - lay_.host.left : 0,
                lay_.host.bottom - lay_.host.top > 0 ? lay_.host.bottom - lay_.host.top : 0,
                TRUE);
            ShowWindow(browser_hwnd, SW_SHOW);
        } else {
            ShowWindow(browser_hwnd, SW_HIDE);
        }
    }
    InvalidateRect(hwnd_, nullptr, FALSE);
}

int CefShell::tab_width_for(int n) const {
    if (n < 1) n = 1;
    int dpi = lay_.dpi;
    int gap = MulDiv(2, dpi, 96);
    int add_w = MulDiv(28, dpi, 96) + gap;
    int avail = (lay_.tab_area.right - lay_.tab_area.left) - add_w;
    if (avail < MulDiv(80, dpi, 96)) avail = MulDiv(80, dpi, 96);
    int tab_w = avail / n - gap;
    int min_w = MulDiv(120, dpi, 96);
    int max_w = MulDiv(240, dpi, 96);
    if (tab_w > max_w) tab_w = max_w;
    if (tab_w < min_w) tab_w = min_w;
    return tab_w;
}

int CefShell::visible_tab_count() const {
    int n = ws_tab_count();
    if (!n) return 0;
    if (lay_.zen) return n;
    int dpi = lay_.dpi;
    int gap = MulDiv(2, dpi, 96);
    int tab_w = tab_width_for(n);
    int avail = lay_.tab_area.right - lay_.tab_area.left;
    int maxv = avail / (tab_w + gap);
    if (maxv < 1) maxv = 1;
    return n < maxv ? n : maxv;
}

int CefShell::max_tab_scroll() const {
    int n = ws_tab_count();
    if (n < 2) return 0;
    int dpi = lay_.dpi;
    if (lay_.zen) {
        int pitch = MulDiv(38, dpi, 96);
        int content = n * pitch;
        int avail = lay_.tab_area.bottom - lay_.tab_area.top;
        return content > avail ? content - avail : 0;
    }
    int gap = MulDiv(2, dpi, 96);
    int tab_w = tab_width_for(n);
    int content = n * (tab_w + gap);
    int avail = (lay_.tab_area.right - lay_.tab_area.left)
        - (MulDiv(28, dpi, 96) + gap);
    return content > avail ? content - avail : 0;
}

void CefShell::clamp_tab_scroll() {
    int maxs = max_tab_scroll();
    if (tab_scroll_ < 0) tab_scroll_ = 0;
    if (tab_scroll_ > maxs) tab_scroll_ = maxs;
}

void CefShell::scroll_tabs(int delta) {
    tab_scroll_ += delta;
    clamp_tab_scroll();
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void CefShell::ensure_tab_visible(int index) {
    int slot = slot_of_tab(index);
    if (slot < 0) return;
    int n = ws_tab_count();
    (void)n;
    int maxs = max_tab_scroll();
    if (maxs <= 0) {
        tab_scroll_ = 0;
        return;
    }
    int dpi = lay_.dpi;
    if (lay_.zen) {
        int pitch = MulDiv(38, dpi, 96);
        int height = MulDiv(34, dpi, 96);
        int top_eff = lay_.tab_area.top + slot * pitch;
        if (top_eff - tab_scroll_ < lay_.tab_area.top) {
            tab_scroll_ = top_eff - lay_.tab_area.top;
        }
        if (top_eff + height - tab_scroll_ > lay_.tab_area.bottom) {
            tab_scroll_ = top_eff + height - lay_.tab_area.bottom;
        }
    } else {
        int gap = MulDiv(2, dpi, 96);
        int tab_w = tab_width_for(ws_tab_count());
        int left_eff = lay_.tab_area.left + slot * (tab_w + gap);
        int margin = MulDiv(30, dpi, 96);
        if (left_eff - tab_scroll_ < lay_.tab_area.left) {
            tab_scroll_ = left_eff - lay_.tab_area.left;
        }
        if (left_eff + tab_w - tab_scroll_ > lay_.tab_area.right - margin) {
            tab_scroll_ = left_eff + tab_w - lay_.tab_area.right + margin;
        }
    }
    clamp_tab_scroll();
}

RECT CefShell::tab_thumb_rect() const {
    int maxs = max_tab_scroll();
    if (maxs <= 0) return {};
    int n = ws_tab_count();
    int dpi = lay_.dpi;
    RECT area = lay_.tab_area;
    if (lay_.zen) {
        int track = area.bottom - area.top;
        int content = n * MulDiv(38, dpi, 96);
        int len = track > 0 ? MulDiv(track, track, content) : 0;
        int min_len = MulDiv(24, dpi, 96);
        if (len < min_len) len = min_len;
        if (len > track) len = track;
        int y = area.top + MulDiv(tab_scroll_, track - len, maxs);
        // The thumb hugs the host-facing edge (left edge when the rail sits
        // on the right side of the window).
        if (rail_side_ == "right") {
            int left = area.left + MulDiv(2, dpi, 96);
            return {left, y, left + MulDiv(4, dpi, 96), y + len};
        }
        int right = area.right - MulDiv(2, dpi, 96);
        return {right - MulDiv(4, dpi, 96), y, right, y + len};
    }
    int gap = MulDiv(2, dpi, 96);
    int track = area.right - area.left;
    int content = n * (tab_width_for(n) + gap);
    int len = track > 0 ? MulDiv(track, track, content) : 0;
    int min_len = MulDiv(24, dpi, 96);
    if (len < min_len) len = min_len;
    if (len > track) len = track;
    int x = area.left + MulDiv(tab_scroll_, track - len, maxs);
    return {x, area.bottom - MulDiv(5, dpi, 96), x + len, area.bottom - MulDiv(1, dpi, 96)};
}

RECT CefShell::rail_grip_rect() const {
    // Zen only: a narrow strip along the rail edge that faces the page host.
    // Zen chips stop MulDiv(6, dpi) short of that edge, so the strip never
    // sits under a tab; the scroll thumb overlaps it, and WM_LBUTTONDOWN
    // checks the thumb first — scroll wins where both exist.
    if (!lay_.zen) return {};
    int g = MulDiv(6, lay_.dpi, 96);
    if (g < 5) g = 5;
    if (rail_side_ == "right") {
        return {lay_.rail.left, lay_.rail.top, lay_.rail.left + g,
                lay_.rail.bottom};
    }
    return {lay_.rail.right - g, lay_.rail.top, lay_.rail.right,
            lay_.rail.bottom};
}

void CefShell::flip_rail_side(const std::string& side) {
    if (rail_side_ == side) return;
    rail_side_ = side;
    Runtime* rt = runtime();
    if (rt) kiwi::save_shell_rail_side(rt->config_dir, side);
    sync_settings_rail_side();
    layout();
    InvalidateRect(hwnd_, nullptr, FALSE);
}

int CefShell::drop_slot_at(const POINT& cl) const {
    int n = ws_tab_count();
    if (n < 2) return 0;
    int dpi = lay_.dpi;
    if (lay_.zen) {
        int pitch = MulDiv(38, dpi, 96);
        int ghost_top_eff = cl.y - grab_dy_ + tab_scroll_;
        int slot = (ghost_top_eff - lay_.tab_area.top + pitch / 2) / pitch;
        if (slot < 0) slot = 0;
        if (slot > n - 1) slot = n - 1;
        return slot;
    }
    int gap = MulDiv(2, dpi, 96);
    int pitch = tab_width_for(n) + gap;
    int ghost_left_eff = cl.x - grab_dx_ + tab_scroll_;
    int slot = (ghost_left_eff - lay_.tab_area.left + pitch / 2) / pitch;
    if (slot < 0) slot = 0;
    if (slot > n - 1) slot = n - 1;
    return slot;
}

RECT CefShell::drag_ghost_rect() const {
    int n = ws_tab_count();
    int dslot = slot_of_tab(drag_tab_);
    if (dslot < 0) dslot = 0;
    RECT chip = tab_chip_rect(dslot, tab_width_for(n));
    if (lay_.zen) {
        int height = chip.bottom - chip.top;
        chip.top = drag_pos_.y - grab_dy_;
        chip.bottom = chip.top + height;
        return chip;
    }
    int width = chip.right - chip.left;
    int slot = drop_slot_ >= 0 && drop_slot_ < n ? drop_slot_ : dslot;
    RECT row = tab_chip_rect(slot, tab_width_for(n));
    chip.left = drag_pos_.x - grab_dx_;
    chip.right = chip.left + width;
    chip.top = row.top;
    chip.bottom = row.bottom;
    return chip;
}

void CefShell::move_tab(int from, int to) {
    // |from| is a global tab index; |to| is a visual slot inside the active
    // workspace. The reorder happens inside the workspace subset; tabs of
    // other workspaces keep their relative order behind it.
    std::vector<int> list = ws_tabs();
    int from_slot = -1;
    for (size_t i = 0; i < list.size(); ++i) {
        if (list[i] == from) from_slot = static_cast<int>(i);
    }
    if (from_slot < 0) return;
    if (to < 0) to = 0;
    if (to >= static_cast<int>(list.size())) {
        to = static_cast<int>(list.size()) - 1;
    }
    if (to == from_slot) return;
    const int active_id = active_ >= 0
        && active_ < static_cast<int>(tabs_.size())
        ? tabs_[static_cast<size_t>(active_)].browser_id : 0;
    int moving = list[static_cast<size_t>(from_slot)];
    list.erase(list.begin() + from_slot);
    list.insert(list.begin() + to, moving);
    // Stable permutation: workspace members land in the new order, every
    // other tab keeps its relative position after them.
    std::vector<int> ranks(tabs_.size(), -1);
    int r = 0;
    for (int g : list) ranks[static_cast<size_t>(g)] = r++;
    for (size_t g = 0; g < tabs_.size(); ++g) {
        if (ranks[g] < 0) ranks[g] = r++;
    }
    std::vector<int> order(tabs_.size());
    for (size_t i = 0; i < order.size(); ++i) order[i] = static_cast<int>(i);
    std::stable_sort(order.begin(), order.end(),
        [&ranks](int a, int b) { return ranks[a] < ranks[b]; });
    std::vector<WinTab> reordered;
    reordered.reserve(tabs_.size());
    for (int g : order) {
        reordered.push_back(std::move(tabs_[static_cast<size_t>(g)]));
    }
    tabs_ = std::move(reordered);
    active_ = active_id ? index_by_id(active_id) : -1;
    ensure_tab_visible(active_);
    layout();
}

bool CefShell::devtools_docked_visible() const {
    if (!devtools_hwnd_ || devtools_tab_id_ == 0) return false;
    if (active_ < 0 || active_ >= static_cast<int>(tabs_.size())) return false;
    return tabs_[static_cast<size_t>(active_)].browser_id == devtools_tab_id_;
}

bool CefShell::devtools_bar_hit(const POINT& cl) const {
    if (!devtools_docked_visible()) return false;
    RECT hit = lay_.devtools_bar;
    int pad = MulDiv(2, lay_.dpi, 96);
    InflateRect(&hit, pad, pad);
    return PtInRect(&hit, cl) != FALSE;
}

void CefShell::set_devtools_dock(int dock) {
    if (dock < DOCK_BOTTOM || dock > DOCK_RIGHT || dock == devtools_dock_) return;
    devtools_dock_ = dock;
    Runtime* rt = runtime();
    save_shell_devtools_dock(rt ? rt->config_dir : std::string(),
        dock == DOCK_LEFT ? "left" : dock == DOCK_RIGHT ? "right" : "bottom");
    layout();
}

void CefShell::open_devtools_dock_menu(const POINT& screen) {
    HMENU menu = CreatePopupMenu();
    AppendMenuW(menu, MF_STRING | (devtools_dock_ == DOCK_BOTTOM ? MF_CHECKED : MF_UNCHECKED),
        kDockMenuBottom, L"Dock to bottom");
    AppendMenuW(menu, MF_STRING | (devtools_dock_ == DOCK_LEFT ? MF_CHECKED : MF_UNCHECKED),
        kDockMenuLeft, L"Dock to left");
    AppendMenuW(menu, MF_STRING | (devtools_dock_ == DOCK_RIGHT ? MF_CHECKED : MF_UNCHECKED),
        kDockMenuRight, L"Dock to right");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, kDockMenuClose, L"Close DevTools");
    SetForegroundWindow(hwnd_);
    int cmd = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON | TPM_NONOTIFY,
        screen.x, screen.y, 0, hwnd_, nullptr);
    DestroyMenu(menu);
    switch (cmd) {
        case kDockMenuBottom:
            set_devtools_dock(DOCK_BOTTOM);
            break;
        case kDockMenuLeft:
            set_devtools_dock(DOCK_LEFT);
            break;
        case kDockMenuRight:
            set_devtools_dock(DOCK_RIGHT);
            break;
        case kDockMenuClose:
            toggle_devtools();
            break;
        default:
            break;
    }
}

bool CefShell::tear_zone_at(const POINT& cl) const {
    RECT zone = lay_.tab_area;
    int margin = MulDiv(12, lay_.dpi, 96);
    zone.left -= margin;
    zone.top -= margin;
    zone.right += margin;
    zone.bottom += margin;
    return PtInRect(&zone, cl) == FALSE;
}

RECT CefShell::tear_ghost_rect() const {
    RECT chip = tab_chip_rect(drag_tab_, tab_width_for(static_cast<int>(tabs_.size())));
    int width = chip.right - chip.left;
    int height = chip.bottom - chip.top;
    RECT ghost;
    ghost.left = drag_pos_.x - grab_dx_;
    ghost.top = drag_pos_.y - grab_dy_;
    ghost.right = ghost.left + width;
    ghost.bottom = ghost.top + height;
    return ghost;
}

void CefShell::show_tear_hint() {
    if (drag_tab_ < 0 || drag_tab_ >= static_cast<int>(tabs_.size())) return;
    RECT chip = tab_chip_rect(drag_tab_, tab_width_for(static_cast<int>(tabs_.size())));
    int w = chip.right - chip.left;
    int h = chip.bottom - chip.top;
    if (w <= 0 || h <= 0) return;
    if (!tear_hint_) {
        tear_hint_ = CreateWindowExW(WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_TOOLWINDOW
            | WS_EX_TOPMOST | WS_EX_NOACTIVATE, L"STATIC", nullptr, WS_POPUP,
            0, 0, 1, 1, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
        if (!tear_hint_) return;
    }
    HDC screen = GetDC(nullptr);
    if (!screen) return;
    HDC mem = CreateCompatibleDC(screen);
    BITMAPINFO info = {};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = w;
    info.bmiHeader.biHeight = -h;
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;
    void* bits = nullptr;
    HBITMAP bmp = CreateDIBSection(mem, &info, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (bmp && bits) {
        HGDIOBJ old_bmp = SelectObject(mem, bmp);
        RECT full = {0, 0, w, h};
        fill_rect_color(mem, full, gfx::palette().active_tab_bg);
        draw_tab(mem, drag_tab_, full);
        auto* pixels = static_cast<uint32_t*>(bits);
        for (int i = 0; i < w * h; ++i) pixels[i] |= 0xFF000000u;
        POINT src = {0, 0};
        POINT pos = {0, 0};
        if (hwnd_) {
            POINT cursor = drag_pos_;
            ClientToScreen(hwnd_, &cursor);
            pos.x = cursor.x - grab_dx_;
            pos.y = cursor.y - grab_dy_;
        }
        SIZE size = {w, h};
        BLENDFUNCTION blend = {AC_SRC_OVER, 0, 255, 0};
        UpdateLayeredWindow(tear_hint_, screen, &pos, &size, mem, &src, 0, &blend,
            ULW_ALPHA);
        SelectObject(mem, old_bmp);
    }
    if (bmp) DeleteObject(bmp);
    DeleteDC(mem);
    ReleaseDC(nullptr, screen);
    ShowWindow(tear_hint_, SW_SHOWNA);
}

void CefShell::move_tear_hint(const POINT& client_pt) {
    if (!tear_hint_ || !hwnd_) return;
    POINT screen = client_pt;
    ClientToScreen(hwnd_, &screen);
    SetWindowPos(tear_hint_, HWND_TOPMOST, screen.x - grab_dx_, screen.y - grab_dy_,
        0, 0, SWP_NOSIZE | SWP_NOACTIVATE);
}

void CefShell::hide_tear_hint() {
    if (tear_hint_) ShowWindow(tear_hint_, SW_HIDE);
}

void CefShell::tear_off_window(int index, const POINT& screen) {
    if (index < 0 || index >= static_cast<int>(tabs_.size())) return;
    WinTab& moving = tabs_[static_cast<size_t>(index)];
    if (devtools_tab_id_ != 0 && moving.browser_id == devtools_tab_id_
        && devtools_browser_) {
        devtools_browser_->GetHost()->CloseDevTools();
    }
    CefShell* window = new CefShell();
    Runtime* rt = runtime();
    int width = rt ? rt->settings.window_width : 1280;
    int height = rt ? rt->settings.window_height : 800;
    RECT area = {0, 0, width, height};
    AdjustWindowRect(&area, WS_OVERLAPPEDWINDOW, FALSE);
    if (screen.x >= 0) {
        window->init_pos_ = screen;
        window->init_pos_.x -= scale_for(hwnd_, 80);
        window->init_pos_.y -= scale_for(hwnd_, 180);
        clamp_to_workarea(window->init_pos_, area.right - area.left,
            area.bottom - area.top);
    }
    if (!window->create_window()) {
        delete window;
        return;
    }
    if (moving.browser) {
        WinTab tab = std::move(moving);
        tabs_.erase(tabs_.begin() + index);
        window->adopt_tab(std::move(tab));
    } else {
        std::string url = moving.url;
        bool private_tab = moving.private_tab;
        tabs_.erase(tabs_.begin() + index);
        if (url.empty() || is_start_page_entry(url)) url = start_uri(private_tab);
        window->add_tab(url, private_tab);
        window->activate_index(0);
    }
    if (active_ > index) --active_;
    if (tabs_.empty()) {
        PostMessageW(hwnd_, WM_CLOSE, 0, 0);
        return;
    }
    activate_index(std::min(std::max(active_, 0),
        static_cast<int>(tabs_.size()) - 1));
}

void CefShell::adopt_tab(WinTab tab) {
    tabs_.push_back(std::move(tab));
    WinTab& adopted = tabs_.back();
    active_ = 0;
    if (adopted.browser) {
        CefRefPtr<CefClient> client = adopted.browser->GetHost()->GetClient();
        auto* kiwi_client = static_cast<KiwiClient*>(client.get());
        if (kiwi_client) kiwi_client->set_host(this);
        HWND browser_hwnd = adopted.browser->GetHost()->GetWindowHandle();
        RemoveWindowSubclass(browser_hwnd, browser_wheel_proc, 2);
        SetWindowSubclass(browser_hwnd, browser_wheel_proc, 2,
            reinterpret_cast<DWORD_PTR>(this));
        adopted.wheel_subclassed = true;
        SetParent(browser_hwnd, host_);
    }
    layout();
    update_omnibox();
    update_window_title();
    if (adopted.browser) {
        SetFocus(adopted.browser->GetHost()->GetWindowHandle());
    }
    InvalidateRect(hwnd_, nullptr, FALSE);
}

RECT CefShell::tab_chip_rect(int index, int tab_w) const {
    int dpi = lay_.dpi;
    RECT rect;
    if (lay_.zen) {
        int pitch = MulDiv(38, dpi, 96);
        int height = MulDiv(34, dpi, 96);
        rect.left = lay_.tab_area.left + MulDiv(6, dpi, 96);
        rect.right = lay_.tab_area.right - MulDiv(6, dpi, 96);
        rect.top = lay_.tab_area.top + index * pitch - tab_scroll_;
        rect.bottom = rect.top + height;
        return rect;
    }
    int gap = MulDiv(2, dpi, 96);
    rect.left = lay_.tab_area.left + index * (tab_w + gap) - tab_scroll_;
    rect.right = rect.left + tab_w;
    rect.top = lay_.tab_area.top + MulDiv(4, dpi, 96);
    rect.bottom = lay_.tab_area.bottom - MulDiv(2, dpi, 96);
    return rect;
}

RECT CefShell::tab_close_rect(const RECT& chip) const {
    int dpi = lay_.dpi;
    int size = MulDiv(18, dpi, 96);
    RECT rect;
    rect.right = chip.right - MulDiv(4, dpi, 96);
    rect.left = rect.right - size;
    rect.top = (chip.top + chip.bottom - size) / 2;
    rect.bottom = rect.top + size;
    return rect;
}

RECT CefShell::add_btn_rect() const {
    int dpi = lay_.dpi;
    int size = MulDiv(28, dpi, 96);
    if (lay_.zen) {
        int add = MulDiv(34, dpi, 96);
        int left = lay_.rail.left + MulDiv(8, dpi, 96);
        int top = ws_add_pill_.bottom > lay_.rail.top
            ? ws_add_pill_.bottom + MulDiv(8, dpi, 96)
            : lay_.rail.top + MulDiv(6, dpi, 96);
        return {left, top, left + add, top + add};
    }
    int ws_n = ws_tab_count();
    if (max_tab_scroll() > 0) {
        int left = lay_.tab_area.right - size - MulDiv(2, dpi, 96);
        int top = (lay_.tab_area.top + lay_.tab_area.bottom - size) / 2;
        return {left, top, left + size, top + size};
    }
    int visible = visible_tab_count();
    int right;
    if (visible > 0) {
        RECT last = tab_chip_rect(visible - 1, tab_width_for(ws_n));
        right = last.right + MulDiv(3, dpi, 96);
    } else {
        right = lay_.tab_area.left + MulDiv(3, dpi, 96);
    }
    int limit = lay_.tab_area.right - size;
    if (right > limit) right = limit;
    int top = (lay_.tab_area.top + lay_.tab_area.bottom - size) / 2;
    return {right, top, right + size, top + size};
}

int CefShell::tab_at(const POINT& cl) const {
    std::vector<int> list = ws_tabs();
    int n = static_cast<int>(list.size());
    if (!n) return -1;
    int dpi = lay_.dpi;
    if (cl.x < lay_.tab_area.left || cl.x > lay_.tab_area.right
        || cl.y < lay_.tab_area.top || cl.y > lay_.tab_area.bottom) {
        return -1;
    }
    if (lay_.zen) {
        int pitch = MulDiv(38, dpi, 96);
        int height = MulDiv(34, dpi, 96);
        int idx = (cl.y + tab_scroll_ - lay_.tab_area.top) / pitch;
        if (idx < 0 || idx >= n) return -1;
        int chip_top = lay_.tab_area.top + idx * pitch - tab_scroll_;
        if (cl.y > chip_top + height) return -1;
        return list[static_cast<size_t>(idx)];
    }
    int gap = MulDiv(2, dpi, 96);
    int tab_w = tab_width_for(n);
    int pitch = tab_w + gap;
    int idx = (cl.x + tab_scroll_ - lay_.tab_area.left) / pitch;
    if (idx < 0 || idx >= n) return -1;
    int x0 = lay_.tab_area.left + idx * pitch - tab_scroll_;
    if (cl.x > x0 + tab_w) return -1;
    return list[static_cast<size_t>(idx)];
}

int CefShell::tool_at(const POINT& cl) const {
    for (int i = 0; i < 5; ++i) {
        if (PtInRect(&lay_.tool_btns[i], cl)) return i;
    }
    return -1;
}

bool CefShell::chrome_is_drag_zone(const POINT& cl) const {
    RECT client;
    GetClientRect(hwnd_, &client);
    if (!PtInRect(&client, cl)) return false;
    if (PtInRect(&lay_.host, cl)) return false;
    if (PtInRect(&lay_.omnibox_pill, cl)) return false;
    if (PtInRect(&lay_.add_btn, cl)) return false;
    if (ws_at(cl) >= 0 || ws_add_hit(cl)) return false;
    for (int i = 0; i < 5; ++i) {
        if (PtInRect(&lay_.tool_btns[i], cl)) return false;
    }
    if (find_visible_) {
        if (PtInRect(&lay_.find_entry, cl) || PtInRect(&lay_.find_prev, cl)
            || PtInRect(&lay_.find_next, cl) || PtInRect(&lay_.find_close, cl)) {
            return false;
        }
    }
    if (tab_at(cl) >= 0) return false;
    RECT thumb = tab_thumb_rect();
    if (!IsRectEmpty(&thumb) && PtInRect(&thumb, cl)) return false;
    RECT grip = rail_grip_rect();
    if (PtInRect(&grip, cl)) return false;
    if (devtools_bar_hit(cl)) return false;
    if (PtInRect(&lay_.min_btn, cl) || PtInRect(&lay_.max_btn, cl)
        || PtInRect(&lay_.close_btn, cl)) {
        return false;
    }
    return true;
}

void CefShell::paint(HDC dc, const RECT& client) {
    if (backdrop_active()) {
        fill_rect_color(dc, client, RGB(0, 0, 0));
        draw_chrome(dc, client, true);
        return;
    }
    HDC mem = CreateCompatibleDC(dc);
    HBITMAP bmp = CreateCompatibleBitmap(dc, client.right - client.left,
        client.bottom - client.top);
    HGDIOBJ old = SelectObject(mem, bmp);
    fill_rect_color(mem, client, gfx::palette().chrome_bg);
    draw_chrome(mem, client, false);
    BitBlt(dc, 0, 0, client.right - client.left, client.bottom - client.top,
        mem, 0, 0, SRCCOPY);
    SelectObject(mem, old);
    DeleteObject(bmp);
    DeleteDC(mem);
}

void CefShell::draw_chrome(HDC dc, const RECT& client, bool direct) {
    (void)client;
    (void)direct;
    draw_rail(dc);
    draw_tabs(dc);
    draw_omnibox(dc);
    draw_tool_buttons(dc, direct);
    draw_find_bar(dc);
    draw_devtools_split(dc);
    draw_status(dc);
    draw_caption_buttons(dc);
}

void CefShell::draw_add_button(HDC dc, const RECT& rect) {
    const gfx::Palette& pal = gfx::palette();
    int dpi = lay_.dpi;
    if (press_add_ || hover_add_) {
        fill_round_rect(dc, rect, 8, dpi,
            press_add_ ? pal.pressed_chip : pal.hover_chip);
    }
    RECT icon = {rect.left + MulDiv(6, dpi, 96), rect.top + MulDiv(6, dpi, 96),
                 rect.right - MulDiv(6, dpi, 96), rect.bottom - MulDiv(6, dpi, 96)};
    gfx::draw_glyph(dc, icon, gfx::Glyph::Plus, pal.chrome_muted, 1.6f, dpi);
}

void CefShell::draw_rail(HDC dc) {
    if (!lay_.zen) return;
    const gfx::Palette& pal = gfx::palette();
    if (!backdrop_active()) {
        // A panel tone one step off the chrome so the rail reads as its own
        // surface (with a backdrop the Mica/acrylic tint does this instead).
        COLORREF tint = gfx::blend(pal.chrome_bg, RGB(0, 0, 0),
                                   gfx::dark_chrome() ? 16 : 4);
        fill_rect_color(dc, lay_.rail, tint);
    }
    // Hairline on the edge that faces the page host.
    RECT line;
    if (rail_side_ == "right") {
        line = {lay_.rail.left, lay_.rail.top,
                lay_.rail.left + MulDiv(1, lay_.dpi, 96), lay_.rail.bottom};
    } else {
        line = {lay_.rail.right - MulDiv(1, lay_.dpi, 96), lay_.rail.top,
                lay_.rail.right, lay_.rail.bottom};
    }
    fill_rect_color(dc, line, pal.outline);

    // Opera-style workspace pills: the first section of the rail. The
    // active one carries the accent wash; click switches, the "+" pill
    // below opens the workspace manager.
    int pill_index = 0;
    for (const WorkspaceDef& def : workspaces_) {
        if (pill_index >= static_cast<int>(ws_pills_.size())) break;
        RECT pill = ws_pills_[static_cast<size_t>(pill_index)];
        const bool active = def.id == active_ws_;
        const bool hot = hover_ws_ == pill_index;
        if (active) {
            COLORREF wash = gfx::blend(pal.active_tab_bg, pal.accent, 22);
            fill_round_rect(dc, pill, 8, lay_.dpi, wash);
            stroke_round_rect(dc, pill, 8, lay_.dpi,
                gfx::blend(pal.active_tab_bg, pal.accent, 110), 1.0f);
        } else if (hot) {
            fill_round_rect(dc, pill, 8, lay_.dpi, pal.hover_chip);
        }
        RECT glyph_box = {pill.left + MulDiv(8, lay_.dpi, 96),
                          (pill.top + pill.bottom) / 2 - MulDiv(8, lay_.dpi, 96),
                          pill.left + MulDiv(24, lay_.dpi, 96),
                          (pill.top + pill.bottom) / 2 + MulDiv(8, lay_.dpi, 96)};
        gfx::Glyph glyph = gfx::Glyph::Globe;
        gfx::glyph_from_name(def.glyph, &glyph);
        gfx::draw_glyph(dc, glyph_box, glyph,
            active ? pal.accent : (hot ? pal.chrome_text : pal.chrome_muted),
            1.4f, lay_.dpi);
        RECT text = {glyph_box.right + MulDiv(6, lay_.dpi, 96), pill.top,
                     pill.right, pill.bottom};
        SetBkMode(dc, TRANSPARENT);
        SelectObject(dc, font_);
        SetTextColor(dc, active ? pal.chrome_text : pal.chrome_muted);
        std::wstring name = wide_from_utf8(def.name);
        DrawTextW(dc, name.c_str(), -1, &text,
            DT_END_ELLIPSIS | DT_SINGLELINE | DT_VCENTER | DT_LEFT);
        ++pill_index;
    }
    if (!IsRectEmpty(&ws_add_pill_)) {
        const bool hot = hover_ws_ == static_cast<int>(workspaces_.size());
        if (hot) fill_round_rect(dc, ws_add_pill_, 8, lay_.dpi, pal.hover_chip);
        RECT glyph_box = {ws_add_pill_.left + MulDiv(8, lay_.dpi, 96),
                          (ws_add_pill_.top + ws_add_pill_.bottom) / 2
                              - MulDiv(7, lay_.dpi, 96),
                          ws_add_pill_.left + MulDiv(22, lay_.dpi, 96),
                          (ws_add_pill_.top + ws_add_pill_.bottom) / 2
                              + MulDiv(7, lay_.dpi, 96)};
        gfx::draw_glyph(dc, glyph_box, gfx::Glyph::Plus,
            hot ? pal.chrome_text : pal.chrome_muted, 1.4f, lay_.dpi);
        RECT text = {glyph_box.right + MulDiv(6, lay_.dpi, 96), ws_add_pill_.top,
                     ws_add_pill_.right, ws_add_pill_.bottom};
        SetBkMode(dc, TRANSPARENT);
        SelectObject(dc, font_);
        SetTextColor(dc, hot ? pal.chrome_text : pal.chrome_muted);
        DrawTextW(dc, L"Add workspace", -1, &text,
            DT_END_ELLIPSIS | DT_SINGLELINE | DT_VCENTER | DT_LEFT);
        RECT sep = {lay_.rail.left + MulDiv(8, lay_.dpi, 96),
                    ws_add_pill_.bottom + MulDiv(4, lay_.dpi, 96),
                    lay_.rail.right - MulDiv(8, lay_.dpi, 96),
                    ws_add_pill_.bottom + MulDiv(5, lay_.dpi, 96)};
        fill_rect_color(dc, sep, pal.separator);
    }

    // Drag affordance: a small pill centered on the host-facing edge. It
    // grabs the rail (SIZEWE cursor) and flips the rail side on a horizontal
    // drag; it brightens into the accent while hovered or dragged.
    RECT grip = rail_grip_rect();
    if (IsRectEmpty(&grip)) return;
    bool hot = hover_rail_grip_ || drag_rail_;
    int pw = MulDiv(hot ? 4 : 3, lay_.dpi, 96);
    int ph = MulDiv(hot ? 40 : 30, lay_.dpi, 96);
    int cy = (lay_.rail.top + lay_.rail.bottom) / 2;
    RECT pill;
    if (rail_side_ == "right") {
        pill = {grip.left + MulDiv(1, lay_.dpi, 96), cy - ph / 2,
                grip.left + MulDiv(1, lay_.dpi, 96) + pw, cy + ph / 2};
    } else {
        pill = {grip.right - MulDiv(1, lay_.dpi, 96) - pw, cy - ph / 2,
                grip.right - MulDiv(1, lay_.dpi, 96), cy + ph / 2};
    }
    fill_round_rect(dc, pill, 2, lay_.dpi,
                    hot ? pal.omnibox_focus : pal.outline);
}

void CefShell::draw_devtools_split(HDC dc) {
    if (!devtools_docked_visible()) return;
    fill_rect_color(dc, lay_.devtools_bar, gfx::palette().separator);
}

void CefShell::draw_tabs(HDC dc) {
    if (!lay_.zen) draw_ws_chips(dc);
    std::vector<int> list = ws_tabs();
    int n = static_cast<int>(list.size());
    draw_add_button(dc, lay_.add_btn);
    if (!n) {
        // Empty workspace: a quiet hint under the add button (zen only —
        // the classic strip is too small for one).
        if (lay_.zen && drag_tab_ < 0) {
            const gfx::Palette& pal = gfx::palette();
            RECT hint = {lay_.tab_area.left + MulDiv(12, lay_.dpi, 96),
                         lay_.tab_area.top + MulDiv(6, lay_.dpi, 96),
                         lay_.tab_area.right,
                         lay_.tab_area.top + MulDiv(44, lay_.dpi, 96)};
            SetBkMode(dc, TRANSPARENT);
            SelectObject(dc, font_);
            SetTextColor(dc, pal.chrome_muted);
            DrawTextW(dc, L"No tabs in this workspace", -1, &hint,
                DT_END_ELLIPSIS | DT_SINGLELINE | DT_LEFT | DT_VCENTER);
        }
        return;
    }
    bool reorder_drag = drag_active_ && !drag_tear_ && slot_of_tab(drag_tab_) >= 0;
    int drag_slot = reorder_drag ? slot_of_tab(drag_tab_) : -1;
    std::vector<int> order(static_cast<size_t>(n));
    for (int s = 0; s < n; ++s) order[static_cast<size_t>(s)] = s;
    if (reorder_drag) {
        order.erase(order.begin() + drag_slot);
        int slot = drop_slot_ >= 0 && drop_slot_ < n ? drop_slot_ : n - 1;
        order.insert(order.begin() + slot, drag_slot);
    }
    int saved = SaveDC(dc);
    IntersectClipRect(dc, lay_.tab_area.left, lay_.tab_area.top,
        lay_.tab_area.right, lay_.tab_area.bottom);
    int tab_w = tab_width_for(n);
    for (int slot = 0; slot < n; ++slot) {
        int s = order[static_cast<size_t>(slot)];
        if (reorder_drag && s == drag_slot) continue;
        draw_tab(dc, list[static_cast<size_t>(s)], tab_chip_rect(s, tab_w));
    }
    if (reorder_drag) {
        draw_tab(dc, drag_tab_, drag_ghost_rect());
    }
    RestoreDC(dc, saved);
    if (drag_tear_ && drag_tab_ >= 0 && drag_tab_ < static_cast<int>(tabs_.size())) {
        RECT ghost = tear_ghost_rect();
        draw_tab(dc, drag_tab_, ghost);
        stroke_round_rect(dc, ghost, 8, lay_.dpi, gfx::palette().omnibox_focus, 1.0f);
    }
    RECT thumb = tab_thumb_rect();
    if (!IsRectEmpty(&thumb)) {
        fill_round_rect(dc, thumb, 2, lay_.dpi, gfx::palette().chrome_muted);
    }
}

void CefShell::draw_ws_chips(HDC dc) {
    const gfx::Palette& pal = gfx::palette();
    int dpi = lay_.dpi;
    for (size_t i = 0; i < workspaces_.size() && i < ws_chips_.size(); ++i) {
        const WorkspaceDef& def = workspaces_[i];
        const RECT& chip = ws_chips_[i];
        const bool active = def.id == active_ws_;
        const bool hot = hover_ws_ == static_cast<int>(i);
        if (active || hot) {
            fill_round_rect(dc, chip, 6, dpi,
                active ? gfx::blend(pal.active_tab_bg, pal.accent, 22)
                       : pal.hover_chip);
        }
        RECT glyph_box = {chip.left + MulDiv(4, dpi, 96),
                          chip.top + MulDiv(4, dpi, 96),
                          chip.right - MulDiv(4, dpi, 96),
                          chip.bottom - MulDiv(4, dpi, 96)};
        gfx::Glyph glyph = gfx::Glyph::Globe;
        gfx::glyph_from_name(def.glyph, &glyph);
        gfx::draw_glyph(dc, glyph_box, glyph,
            active ? pal.accent : (hot ? pal.chrome_text : pal.chrome_muted),
            1.3f, dpi);
    }
}

void CefShell::draw_tab(HDC dc, int index, const RECT& chip) {
    const gfx::Palette& pal = gfx::palette();
    int dpi = lay_.dpi;
    const WinTab& tab = tabs_[index];
    bool selected = index == active_;
    bool hovered = hover_tab_ == index;
    if (selected) {
        if (lay_.zen) {
            COLORREF wash = gfx::blend(pal.active_tab_bg, pal.accent, 22);
            fill_round_rect(dc, chip, 10, dpi, wash);
            stroke_round_rect(dc, chip, 10, dpi,
                gfx::blend(pal.active_tab_bg, pal.accent, 110), 1.0f);
        } else {
            fill_round_rect(dc, chip, 8, dpi, pal.active_tab_bg);
            stroke_round_rect(dc, chip, 8, dpi, pal.outline, 1.0f);
        }
    } else if (hovered && press_close_tab_ != index) {
        fill_round_rect(dc, chip, lay_.zen ? 10 : 8, dpi, pal.hover_chip);
    }

    RECT close = tab_close_rect(chip);
    bool close_hot = (hovered && hover_close_) || press_close_tab_ == index;

    RECT icon = {chip.left + MulDiv(8, dpi, 96),
                 (chip.top + chip.bottom) / 2 - MulDiv(8, dpi, 96),
                 chip.left + MulDiv(24, dpi, 96),
                 (chip.top + chip.bottom) / 2 + MulDiv(8, dpi, 96)};
    if (tab.loading) {
        gfx::draw_glyph(dc, icon, gfx::Glyph::Spinner, pal.accent, 1.6f, dpi,
                        spin_phase_);
    } else {
        void* image = tab.private_tab ? nullptr
                                      : favicons::bitmap(tab.favicon_host);
        if (image) {
            gfx::draw_image_fit(dc, icon, image);
        } else if (tab.private_tab) {
            gfx::draw_glyph(dc, icon, gfx::Glyph::Incognito, pal.chrome_muted,
                            1.4f, dpi);
        } else {
            gfx::draw_glyph(dc, icon, gfx::Glyph::Globe, pal.chrome_muted, 1.4f, dpi);
        }
    }

    RECT text_rect = {icon.right + MulDiv(6, dpi, 96), chip.top,
                      close.left - MulDiv(2, dpi, 96), chip.bottom};
    std::wstring title = wide_from_utf8(
        tab.title.empty() ? std::string(tab.private_tab ? "New private tab" : "New tab")
                          : tab.title);
    SetBkMode(dc, TRANSPARENT);
    SelectObject(dc, font_);
    SetTextColor(dc, selected ? pal.chrome_text : pal.chrome_muted);
    DrawTextW(dc, title.c_str(), -1, &text_rect,
        DT_END_ELLIPSIS | DT_SINGLELINE | DT_VCENTER | DT_LEFT);

    if (selected || hovered) {
        if (close_hot) {
            fill_round_rect(dc, close, 9, dpi,
                press_close_tab_ == index ? pal.pressed_chip : pal.hover_chip);
        }
        RECT glyph = {close.left + MulDiv(3, dpi, 96), close.top + MulDiv(3, dpi, 96),
                      close.right - MulDiv(3, dpi, 96),
                      close.bottom - MulDiv(3, dpi, 96)};
        gfx::draw_glyph(dc, glyph, gfx::Glyph::Close,
            close_hot ? pal.chrome_text : pal.chrome_muted, 1.4f, dpi);
    }
}

void CefShell::draw_omnibox(HDC dc) {
    const gfx::Palette& pal = gfx::palette();
    int dpi = lay_.dpi;
    bool focused = GetFocus() == omnibox_;
    fill_round_rect(dc, lay_.omnibox_pill, MulDiv(15, dpi, 96), dpi, pal.omnibox_bg);
    stroke_round_rect(dc, lay_.omnibox_pill, MulDiv(15, dpi, 96), dpi,
        focused ? pal.omnibox_focus : pal.omnibox_border, 1.0f);

    // Kiwi Shields: the glyph turns accent while the page keeps its shields
    // up, goes muted when the site is excepted or blocking is off, and a
    // small counter shows this page's blocked hits (Brave-style).
    const WinTab* tab = active_ >= 0 && active_ < static_cast<int>(tabs_.size())
        ? &tabs_[static_cast<size_t>(active_)] : nullptr;
    Runtime* rt = runtime();
    std::string page = tab && !tab->url.empty() && !is_start_page_entry(tab->url)
        ? url_host(tab->url) : std::string();
    const bool enabled = rt && rt->shields_cfg.enabled;
    const bool down = enabled && !page.empty()
        && shields::shields_site_mode(rt->shields_cfg, page) == "down";
    if (press_shield_ || hover_shield_) {
        fill_round_rect(dc, lay_.shield_btn, MulDiv(11, dpi, 96), dpi,
            press_shield_ ? pal.pressed_chip : pal.hover_chip);
    }
    gfx::draw_glyph(dc, lay_.shield_btn, gfx::Glyph::Shield,
        down || !enabled ? pal.chrome_muted : pal.accent, 1.5f, dpi);
    const int count = tab ? tab->shield_count : 0;
    if (enabled && !down && count > 0) {
        RECT label = {lay_.shield_btn.left - MulDiv(46, dpi, 96),
                      lay_.shield_btn.top - MulDiv(2, dpi, 96),
                      lay_.shield_btn.left - MulDiv(4, dpi, 96),
                      lay_.shield_btn.bottom + MulDiv(2, dpi, 96)};
        SetBkMode(dc, TRANSPARENT);
        SelectObject(dc, font_);
        SetTextColor(dc, pal.chrome_muted);
        std::wstring text = std::to_wstring(count);
        DrawTextW(dc, text.c_str(), -1, &label,
            DT_END_ELLIPSIS | DT_SINGLELINE | DT_RIGHT | DT_VCENTER);
    }
}

void CefShell::draw_tool_buttons(HDC dc, bool direct) {
    (void)direct;
    const gfx::Palette& pal = gfx::palette();
    int dpi = lay_.dpi;
    CefRefPtr<CefBrowser> browser = active_browser();
    const gfx::Glyph glyphs[5] = {gfx::Glyph::Back, gfx::Glyph::Forward,
                                  gfx::Glyph::Reload, gfx::Glyph::Star,
                                  gfx::Glyph::Gear};
    for (int i = 0; i < 5; ++i) {
        bool enabled = true;
        if (i == 0) enabled = browser && browser->CanGoBack();
        if (i == 1) enabled = browser && browser->CanGoForward();
        if (hover_tool_ == i || press_tool_ == i) {
            fill_round_rect(dc, lay_.tool_btns[i], MulDiv(17, dpi, 96), dpi,
                press_tool_ == i ? pal.pressed_chip : pal.hover_chip);
        }
        COLORREF color = enabled ? pal.chrome_text : pal.chrome_muted;
        RECT icon = lay_.tool_btns[i];
        int inset = MulDiv(8, dpi, 96);
        icon.left += inset;
        icon.top += inset;
        icon.right -= inset;
        icon.bottom -= inset;
        if (i == 3 && star_active_) {
            gfx::draw_glyph(dc, icon, gfx::Glyph::StarFilled, pal.accent, 1.5f, dpi);
        } else {
            gfx::draw_glyph(dc, icon, glyphs[i], color, 1.5f, dpi);
        }
    }
}

void CefShell::draw_find_bar(HDC dc) {
    if (!find_visible_) return;
    const gfx::Palette& pal = gfx::palette();
    int dpi = lay_.dpi;
    RECT pill = {lay_.find_entry.left - MulDiv(8, dpi, 96),
                 lay_.find_entry.top - MulDiv(1, dpi, 96),
                 lay_.find_entry.right + MulDiv(8, dpi, 96),
                 lay_.find_entry.bottom + MulDiv(1, dpi, 96)};
    fill_round_rect(dc, pill, MulDiv(10, dpi, 96), dpi, pal.omnibox_bg);
    stroke_round_rect(dc, pill, MulDiv(10, dpi, 96), dpi, pal.omnibox_border, 1.0f);
    RECT label = lay_.find_label;
    SetBkMode(dc, TRANSPARENT);
    SelectObject(dc, font_);
    SetTextColor(dc, pal.chrome_muted);
    std::wstring text = wide_from_utf8(find_label_text_);
    DrawTextW(dc, text.c_str(), -1, &label,
        DT_END_ELLIPSIS | DT_SINGLELINE | DT_VCENTER | DT_LEFT);

    const RECT* rects[3] = {&lay_.find_prev, &lay_.find_next, &lay_.find_close};
    const gfx::Glyph glyphs[3] = {gfx::Glyph::Back, gfx::Glyph::Forward,
                                  gfx::Glyph::Close};
    for (int i = 0; i < 3; ++i) {
        RECT icon = *rects[i];
        int inset = MulDiv(6, dpi, 96);
        icon.left += inset;
        icon.top += inset;
        icon.right -= inset;
        icon.bottom -= inset;
        gfx::draw_glyph(dc, icon, glyphs[i], pal.chrome_muted, 1.5f, dpi);
    }
}

void CefShell::draw_status(HDC dc) {
    if (status_text_.empty() || lay_.status.bottom <= lay_.status.top) return;
    const gfx::Palette& pal = gfx::palette();
    RECT line = {lay_.status.left, lay_.status.top, lay_.status.right,
                 lay_.status.top + MulDiv(1, lay_.dpi, 96)};
    fill_rect_color(dc, line, pal.separator);
    RECT text = lay_.status;
    text.left += MulDiv(10, lay_.dpi, 96);
    SetBkMode(dc, TRANSPARENT);
    SelectObject(dc, font_);
    SetTextColor(dc, pal.chrome_muted);
    std::wstring wide = wide_from_utf8(status_text_);
    DrawTextW(dc, wide.c_str(), -1, &text,
        DT_END_ELLIPSIS | DT_SINGLELINE | DT_VCENTER | DT_LEFT);
}

void CefShell::draw_caption_buttons(HDC dc) {
    const gfx::Palette& pal = gfx::palette();
    int dpi = lay_.dpi;
    const RECT* rects[3] = {&lay_.min_btn, &lay_.max_btn, &lay_.close_btn};
    for (int i = 0; i < 3; ++i) {
        if (hover_caption_ != i) continue;
        COLORREF bg = i == 2 ? pal.danger_hover : pal.hover_chip;
        fill_round_rect(dc, *rects[i], 0, dpi, bg);
    }
    RECT icon = lay_.min_btn;
    int inset = MulDiv(16, dpi, 96);
    icon.left += inset;
    icon.right -= inset;
    icon.top += (lay_.min_btn.bottom - lay_.min_btn.top - (icon.right - icon.left)) / 2;
    icon.bottom = icon.top + (icon.right - icon.left);
    gfx::draw_glyph(dc, icon, gfx::Glyph::Minimize, pal.chrome_muted, 1.2f, dpi);

    icon = lay_.max_btn;
    icon.left += inset;
    icon.right -= inset;
    icon.top += (lay_.max_btn.bottom - lay_.max_btn.top - (icon.right - icon.left)) / 2;
    icon.bottom = icon.top + (icon.right - icon.left);
    gfx::draw_glyph(dc, icon, IsZoomed(hwnd_) ? gfx::Glyph::Restore : gfx::Glyph::Maximize,
        pal.chrome_muted, 1.2f, dpi);

    icon = lay_.close_btn;
    icon.left += inset;
    icon.right -= inset;
    icon.top += (lay_.close_btn.bottom - lay_.close_btn.top - (icon.right - icon.left)) / 2;
    icon.bottom = icon.top + (icon.right - icon.left);
    gfx::draw_glyph(dc, icon, gfx::Glyph::CaptionClose,
        hover_caption_ == 2 ? RGB(255, 255, 255) : pal.chrome_muted, 1.2f, dpi);
}

void CefShell::open_tab_context_menu(int index, const POINT& screen) {
    HMENU menu = CreatePopupMenu();
    if (index < 0 || index >= static_cast<int>(tabs_.size())) {
        AppendMenuW(menu, MF_STRING, TABCTX_NEW, L"New tab");
    } else {
        const WinTab& tab = tabs_[index];
        AppendMenuW(menu, MF_STRING, TABCTX_RELOAD, L"Reload");
        AppendMenuW(menu, MF_STRING, TABCTX_DUPLICATE, L"Duplicate tab");
        AppendMenuW(menu, MF_STRING, TABCTX_DETACH, L"Move tab to new window");
        if (workspaces_.size() > 1) {
            HMENU ws_menu = CreatePopupMenu();
            for (size_t i = 0; i < workspaces_.size(); ++i) {
                const WorkspaceDef& def = workspaces_[i];
                std::wstring label = wide_from_utf8(def.name);
                AppendMenuW(ws_menu,
                    MF_STRING | (def.id == tab.workspace ? MF_CHECKED : 0),
                    TABCTX_WS_BASE + static_cast<int>(i), label.c_str());
            }
            AppendMenuW(menu, MF_POPUP,
                reinterpret_cast<UINT_PTR>(ws_menu), L"Move to workspace");
        }
        AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(menu, MF_STRING, TABCTX_CLOSE, L"Close tab");
        AppendMenuW(menu, MF_STRING, TABCTX_CLOSE_OTHERS, L"Close other tabs");
        if (tabs_.size() <= 1) {
            EnableMenuItem(menu, TABCTX_CLOSE_OTHERS, MF_BYCOMMAND | MF_GRAYED);
        }
        bool start = tab.url.empty() || is_start_page_entry(tab.url);
        if (start) EnableMenuItem(menu, TABCTX_RELOAD, MF_BYCOMMAND | MF_GRAYED);
        if (tab.url.empty()) {
            EnableMenuItem(menu, TABCTX_DUPLICATE, MF_BYCOMMAND | MF_GRAYED);
        }
    }
    SetForegroundWindow(hwnd_);
    int cmd = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON | TPM_NONOTIFY,
        screen.x, screen.y, 0, hwnd_, nullptr);
    DestroyMenu(menu);
    if (cmd == TABCTX_NEW) {
        on_hotkey(HK_NEW_TAB);
        return;
    }
    if (index < 0 || index >= static_cast<int>(tabs_.size())) return;
    if (cmd >= TABCTX_WS_BASE
        && cmd < TABCTX_WS_BASE + static_cast<int>(workspaces_.size())) {
        move_tab_to_workspace(index,
            workspaces_[static_cast<size_t>(cmd - TABCTX_WS_BASE)].id);
        return;
    }
    switch (cmd) {
        case TABCTX_RELOAD:
            if (tabs_[index].browser) tabs_[index].browser->Reload();
            break;
        case TABCTX_DUPLICATE: {
            std::string url = tabs_[index].url;
            bool private_tab = tabs_[index].private_tab;
            int ws = tabs_[index].workspace;
            if (url.empty() || is_start_page_entry(url)) break;
            int next = add_tab(url, private_tab, ws);
            activate_index(next);
            break;
        }
        case TABCTX_DETACH: {
            POINT anywhere = {-1, -1};
            tear_off_window(index, anywhere);
            break;
        }
        case TABCTX_CLOSE:
            close_index(index);
            break;
        case TABCTX_CLOSE_OTHERS:
            close_others(index);
            break;
        default:
            break;
    }
}

std::string CefShell::start_uri(bool private_tab) {
    std::string uri = runtime()->start_page_uri(private_tab);
    return uri.empty() ? std::string("https://example.org") : uri;
}

int CefShell::add_tab(const std::string& url, bool private_tab, int workspace) {
    WinTab tab;
    tab.private_tab = private_tab;
    tab.url = url;
    tab.workspace = workspace > 0 ? workspace : active_ws_;
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
    // Windowed child-HWND hosting is only supported by the Alloy runtime in
    // CEF 152; the Chrome runtime (the default) crashes in
    // TabInterface::GetFromContents while attaching the first tab (verified
    // against the CEF 152 release PDB: the AV stack is
    // CefBrowserHostBase::Create -> CefWindowImpl::Create -> AddTabAt ->
    // WebView::SetWebContents -> WebappsClientDesktop::GetAppIdForWebContents
    // -> TabInterface::GetFromContents).
    info.runtime_style = CEF_RUNTIME_STYLE_ALLOY;
    CefBrowserSettings browser_settings;
    Runtime* rt = runtime();
    if (rt && !rt->settings.javascript_enabled) {
        browser_settings.javascript = STATE_DISABLED;
    }

    CefBrowserHost::CreateBrowser(info, client, url, browser_settings, nullptr, context);
    int position = static_cast<int>(tabs_.size()) - 1;
    layout();
    return position;
}

void CefShell::activate_index(int index) {
    if (index < 0 || index >= static_cast<int>(tabs_.size())) return;
    active_ = index;
    ensure_tab_visible(index);
    layout();
    update_omnibox();
    update_window_title();
    Runtime* rt = runtime();
    star_active_ = false;
    if (rt && rt->bookmarks) {
        const WinTab& tab = tabs_[active_];
        star_active_ = !tab.url.empty() && !is_start_page_entry(tab.url)
            && rt->bookmarks->contains(tab.url);
    }
    if (CefRefPtr<CefBrowser> browser = active_browser()) {
        SetFocus(browser->GetHost()->GetWindowHandle());
    }
    if (find_visible_) refresh_find_bar();
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void CefShell::close_index(int index) {
    if (index < 0 || index >= static_cast<int>(tabs_.size())) return;
    WinTab& tab = tabs_[index];
    if (tab.browser) {
        tab.browser->GetHost()->CloseBrowser(false);
        return;
    }
    const bool was_active = index == active_;
    const int closed_ws = tab.workspace;
    tabs_.erase(tabs_.begin() + index);
    if (tabs_.empty()) {
        DestroyWindow(hwnd_);
        return;
    }
    if (was_active) {
        // Stay inside the workspace: nearest remaining neighbor, else the
        // empty-workspace state.
        int target = -1;
        for (int g = index; g < static_cast<int>(tabs_.size()); ++g) {
            if (tabs_[static_cast<size_t>(g)].workspace == closed_ws) {
                target = g;
                break;
            }
        }
        if (target < 0) {
            for (int g = index - 1; g >= 0; --g) {
                if (tabs_[static_cast<size_t>(g)].workspace == closed_ws) {
                    target = g;
                    break;
                }
            }
        }
        if (target >= 0) {
            activate_index(target);
        } else {
            active_ = -1;
            layout();
            update_omnibox();
            update_window_title();
        }
        return;
    }
    if (active_ > index) --active_;
    layout();
    update_omnibox();
    update_window_title();
}

void CefShell::close_active_tab() {
    close_index(active_);
}

void CefShell::close_others(int keep_index) {
    if (keep_index < 0 || keep_index >= static_cast<int>(tabs_.size())) return;
    const int ws = tabs_[static_cast<size_t>(keep_index)].workspace;
    for (int i = static_cast<int>(tabs_.size()) - 1; i >= 0; --i) {
        if (i != keep_index
            && tabs_[static_cast<size_t>(i)].workspace == ws) {
            close_index(i);
        }
    }
    activate_index(keep_index);
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
        star_active_ = false;
    } else {
        rt->bookmarks->add(tab->url, tab->title, std::time(nullptr));
        star_active_ = true;
    }
    InvalidateRect(hwnd_, &lay_.tool_btns[3], FALSE);
}

void CefShell::toggle_devtools() {
    if (CefRefPtr<CefBrowser> docked = devtools_browser_) {
        docked->GetHost()->CloseDevTools();
        return;
    }
    if (devtools_tab_id_ != 0) return;
    CefRefPtr<CefBrowser> browser = active_browser();
    if (!browser) return;
    if (devtools_split_ <= 0) {
        RECT client;
        GetClientRect(hwnd_, &client);
        if (devtools_dock_ == DOCK_BOTTOM) {
            devtools_split_ = MulDiv(client.bottom - client.top, 2, 5);
        } else {
            devtools_split_ = MulDiv(client.right - client.left, 2, 5);
        }
    }
    CefWindowInfo info;
    info.SetAsChild(hwnd_, CefRect(0, 0, 10, 10));
    // Docked DevTools is a child-HWND browser, so it needs the same Alloy
    // runtime style as the tab browsers (windowed hosting is Alloy-only in
    // CEF 152). The layout reserves a bottom area for it once the shell
    // learns the browser was created via the DevToolsClient lifespan hook.
    info.runtime_style = CEF_RUNTIME_STYLE_ALLOY;
    devtools_tab_id_ = browser->GetIdentifier();
    browser->GetHost()->ShowDevTools(info, devtools_client_, CefBrowserSettings(),
        CefPoint());
}

void CefShell::open_find_bar() {
    find_visible_ = true;
    layout();
    refresh_find_bar();
}

void CefShell::close_find_bar() {
    if (CefRefPtr<CefBrowser> browser = active_browser()) {
        browser->GetHost()->StopFinding(true);
    }
    find_visible_ = false;
    find_label_text_.clear();
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
    std::string text;
    if (active_ >= 0 && active_ < static_cast<int>(tabs_.size())) {
        const WinTab& tab = tabs_[active_];
        if (!tab.find_text.empty()) {
            if (tab.find_count > 0) {
                text = std::to_string(tab.find_active) + " of "
                    + std::to_string(tab.find_count);
            } else {
                text = "No matches";
            }
        }
    }
    find_label_text_ = text;
    InvalidateRect(hwnd_, &lay_.find_label, FALSE);
}

void CefShell::update_omnibox() {
    if (active_ < 0 || active_ >= static_cast<int>(tabs_.size())) {
        // Empty workspace: keep the cue banner visible instead of a stale URL.
        if (GetFocus() != omnibox_) SetWindowTextW(omnibox_, L"");
        return;
    }
    if (GetFocus() == omnibox_) return;
    const std::string& url = tabs_[active_].url;
    SetWindowTextW(omnibox_, wide_from_utf8(
        is_start_page_entry(url) ? std::string() : url).c_str());
}

void CefShell::update_window_title() {
    std::wstring title = L"Kiwi";
    if (active_ >= 0 && active_ < static_cast<int>(tabs_.size())) {
        const WinTab& tab = tabs_[active_];
        if (!tab.title.empty()) {
            title = wide_from_utf8(tab.title);
        } else if (tab.private_tab) {
            title = L"Kiwi — private tab";
        }
    }
    SetWindowTextW(hwnd_, title.c_str());
}

void CefShell::save_session_now() {
    if (!primary_) return;
    Runtime* rt = runtime();
    if (!rt || !rt->settings.session_restore) return;
    mustela::SessionData data;
    data.active = active_;
    for (const WinTab& tab : tabs_) {
        mustela::SessionTab entry;
        entry.url = tab.url;
        entry.title = tab.title;
        data.tabs.push_back(std::move(entry));
    }
    mustela::save_session(rt->profile_root + "/session.toml", data);

    // Workspace overlay aligned 1:1 with the session tab order.
    WorkspaceSessionState ws_state;
    ws_state.active_workspace = active_ws_;
    for (const WinTab& tab : tabs_) {
        ws_state.tab_workspaces.push_back(tab.workspace);
    }
    save_workspace_session(rt->profile_root, ws_state);
}

bool CefShell::restore_session() {
    if (!primary_) return false;
    Runtime* rt = runtime();
    if (!rt || !rt->settings.session_restore) return false;
    mustela::SessionData data;
    if (!mustela::load_session(rt->profile_root + "/session.toml", data)) return false;
    if (data.tabs.empty()) return false;
    for (const mustela::SessionTab& entry : data.tabs) {
        if (entry.url.empty() || is_start_page_entry(entry.url)) {
            add_tab(start_uri(false), false);
        } else {
            add_tab(entry.url, false);
            tabs_.back().title = entry.title;
        }
    }
    // Workspace overlay: per-tab assignment aligned with the session order.
    WorkspaceSessionState ws_state;
    if (load_workspace_session(rt->profile_root, ws_state)) {
        for (size_t i = 0; i < tabs_.size()
             && i < ws_state.tab_workspaces.size(); ++i) {
            const int ws = ws_state.tab_workspaces[i];
            tabs_[i].workspace = ws_index_of(ws) >= 0
                ? ws : workspaces_.front().id;
        }
        if (ws_index_of(ws_state.active_workspace) >= 0) {
            active_ws_ = ws_state.active_workspace;
        }
    }
    int first = 0;
    if (data.active >= 0 && data.active < static_cast<int>(tabs_.size())
        && tabs_[static_cast<size_t>(data.active)].workspace == active_ws_) {
        first = data.active;
    } else {
        std::vector<int> list = ws_tabs();
        if (!list.empty()) first = list.front();
    }
    activate_index(first);
    return true;
}

// ---- Opera-style workspaces -----------------------------------------------

void CefShell::load_workspaces_state() {
    Runtime* rt = runtime();
    workspaces_ = rt ? kiwi::load_workspaces(rt->config_dir)
                     : std::vector<WorkspaceDef>{};
    if (workspaces_.empty()) {
        workspaces_.push_back(WorkspaceDef{1, "Personal", "home"});
    }
    if (ws_index_of(active_ws_) < 0) active_ws_ = workspaces_.front().id;
    // Tabs pointing at a removed workspace move to the first one.
    const int fallback = workspaces_.front().id;
    for (WinTab& tab : tabs_) {
        if (ws_index_of(tab.workspace) < 0) tab.workspace = fallback;
    }
}

void CefShell::reload_workspaces_state() {
    load_workspaces_state();
    clamp_tab_scroll();
    layout();
    InvalidateRect(hwnd_, nullptr, FALSE);
}

std::vector<int> CefShell::ws_tabs() const {
    std::vector<int> list;
    for (size_t i = 0; i < tabs_.size(); ++i) {
        if (tabs_[i].workspace == active_ws_) list.push_back(static_cast<int>(i));
    }
    return list;
}

int CefShell::ws_tab_count() const {
    int n = 0;
    for (const WinTab& tab : tabs_) {
        if (tab.workspace == active_ws_) ++n;
    }
    return n;
}

int CefShell::slot_of_tab(int global_index) const {
    if (global_index < 0 || global_index >= static_cast<int>(tabs_.size())) {
        return -1;
    }
    if (tabs_[static_cast<size_t>(global_index)].workspace != active_ws_) return -1;
    int slot = 0;
    for (int i = 0; i < global_index; ++i) {
        if (tabs_[static_cast<size_t>(i)].workspace == active_ws_) ++slot;
    }
    return slot;
}

int CefShell::ws_index_of(int ws_id) const {
    for (size_t i = 0; i < workspaces_.size(); ++i) {
        if (workspaces_[i].id == ws_id) return static_cast<int>(i);
    }
    return -1;
}

int CefShell::ws_at(const POINT& cl) const {
    for (size_t i = 0; i < ws_pills_.size(); ++i) {
        if (PtInRect(&ws_pills_[i], cl)) return static_cast<int>(i);
    }
    for (size_t i = 0; i < ws_chips_.size(); ++i) {
        if (PtInRect(&ws_chips_[i], cl)) return static_cast<int>(i);
    }
    // The zen "+" pill reports as the one-past index.
    if (!IsRectEmpty(&ws_add_pill_) && PtInRect(&ws_add_pill_, cl)) {
        return static_cast<int>(workspaces_.size());
    }
    return -1;
}

bool CefShell::ws_add_hit(const POINT& cl) const {
    return !IsRectEmpty(&ws_add_pill_) && PtInRect(&ws_add_pill_, cl) != FALSE;
}

void CefShell::switch_workspace(int ws_id) {
    if (ws_id == active_ws_ || ws_index_of(ws_id) < 0) return;
    // Remember the current tab so switching back restores it.
    if (active_ >= 0 && active_ < static_cast<int>(tabs_.size())
        && tabs_[static_cast<size_t>(active_)].workspace == active_ws_) {
        ws_active_tab_[active_ws_] =
            tabs_[static_cast<size_t>(active_)].browser_id;
    }
    active_ws_ = ws_id;
    tab_scroll_ = 0;
    int target = -1;
    auto remembered = ws_active_tab_.find(ws_id);
    if (remembered != ws_active_tab_.end() && remembered->second) {
        target = index_by_id(remembered->second);
        if (target >= 0
            && tabs_[static_cast<size_t>(target)].workspace != ws_id) {
            target = -1;  // stale memory (tab moved on or closed)
        }
    }
    if (target < 0) {
        std::vector<int> list = ws_tabs();
        if (!list.empty()) target = list.front();
    }
    if (target >= 0) {
        activate_index(target);
    } else {
        active_ = -1;
        layout();
        update_omnibox();
        update_window_title();
        InvalidateRect(hwnd_, nullptr, FALSE);
    }
}

void CefShell::move_tab_to_workspace(int global_index, int ws_id) {
    if (global_index < 0 || global_index >= static_cast<int>(tabs_.size())) return;
    if (ws_index_of(ws_id) < 0) return;
    WinTab& tab = tabs_[static_cast<size_t>(global_index)];
    if (tab.workspace == ws_id) return;
    const bool was_active = global_index == active_;
    const int old_ws = tab.workspace;
    tab.workspace = ws_id;
    if (was_active) {
        // Activate the nearest tab that stays behind in the old workspace.
        int target = -1;
        const int count = static_cast<int>(tabs_.size());
        for (int step = 1; step <= count && target < 0; ++step) {
            const int behind = global_index - step;
            const int ahead = global_index + step;
            if (behind >= 0
                && tabs_[static_cast<size_t>(behind)].workspace == old_ws) {
                target = behind;
            } else if (ahead < count
                && tabs_[static_cast<size_t>(ahead)].workspace == old_ws) {
                target = ahead;
            }
        }
        if (target >= 0) {
            activate_index(target);
        } else {
            active_ = -1;
            layout();
            update_omnibox();
            update_window_title();
        }
    } else {
        layout();
    }
    InvalidateRect(hwnd_, nullptr, FALSE);
}

// ---- Kiwi Shields ----------------------------------------------------------

int CefShell::shields_session_total() const {
    int total = 0;
    for (const WinTab& tab : tabs_) total += tab.shield_count;
    return total;
}

void CefShell::on_shields(int browser_id, int blocked) {
    WinTab* tab = tab_by_id(browser_id);
    if (!tab) return;
    tab->shield_count = blocked;
    if (index_by_id(browser_id) == active_) {
        InvalidateRect(hwnd_, nullptr, FALSE);
    }
}

void CefShell::open_shields_menu(const POINT& screen) {
    Runtime* rt = runtime();
    if (!rt) return;
    const WinTab* tab = active_ >= 0 && active_ < static_cast<int>(tabs_.size())
        ? &tabs_[static_cast<size_t>(active_)] : nullptr;
    std::string page = tab && !tab->url.empty() && !is_start_page_entry(tab->url)
        ? url_host(tab->url) : std::string();
    const bool down = !page.empty()
        && shields::shields_site_mode(rt->shields_cfg, page) == "down";

    HMENU menu = CreatePopupMenu();
    AppendMenuW(menu, MF_STRING, kShldToggle,
        down ? L"Shields: down for this site" : L"Shields: up for this site");
    if (page.empty()) {
        EnableMenuItem(menu, kShldToggle, MF_BYCOMMAND | MF_GRAYED);
    }
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    std::wstring page_line = L"Blocked on this page: "
        + std::to_wstring(tab ? tab->shield_count : 0);
    std::wstring total_line = L"Blocked in this session: "
        + std::to_wstring(shields_session_total());
    AppendMenuW(menu, MF_STRING | MF_GRAYED, kShldPage, page_line.c_str());
    AppendMenuW(menu, MF_STRING | MF_GRAYED, kShldTotal, total_line.c_str());
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING | (rt->shields_cfg.enabled ? MF_CHECKED : 0),
        kShldGlobal, L"Block ads and trackers");
    AppendMenuW(menu, MF_STRING | (rt->shields_cfg.cosmetic ? MF_CHECKED : 0),
        kShldCosmetic, L"Hide ad placeholders");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, kShldExceptions, L"Shield exceptions...");
    SetForegroundWindow(hwnd_);
    const int cmd = TrackPopupMenu(menu,
        TPM_RETURNCMD | TPM_RIGHTBUTTON | TPM_NONOTIFY,
        screen.x, screen.y, 0, hwnd_, nullptr);
    DestroyMenu(menu);
    switch (cmd) {
        case kShldToggle:
            shields::shields_set_site_mode(rt->shields_cfg, page,
                                           down ? "up" : "down");
            shields::save_shields(rt->config_dir, rt->shields_cfg);
            InvalidateRect(hwnd_, nullptr, FALSE);
            break;
        case kShldGlobal:
            rt->shields_cfg.enabled = !rt->shields_cfg.enabled;
            shields::save_shields(rt->config_dir, rt->shields_cfg);
            InvalidateRect(hwnd_, nullptr, FALSE);
            break;
        case kShldCosmetic:
            rt->shields_cfg.cosmetic = !rt->shields_cfg.cosmetic;
            shields::save_shields(rt->config_dir, rt->shields_cfg);
            InvalidateRect(hwnd_, nullptr, FALSE);
            break;
        case kShldExceptions:
            open_shields_window(hwnd_);
            break;
        default:
            break;
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

void CefShell::on_created(CefRefPtr<CefBrowser> browser) {
    for (WinTab& tab : tabs_) {
        if (!tab.browser) {
            tab.browser = browser;
            tab.browser_id = browser->GetIdentifier();
            if (!closing_) {
                update_favicon_host(tab);
                // Only a tab in the active workspace steals activation;
                // background workspaces keep their current selection.
                if (tab.workspace == active_ws_) {
                    active_ = static_cast<int>(&tab - tabs_.data());
                }
            }
            layout();
            update_omnibox();
            update_window_title();
            if (find_visible_) refresh_find_bar();
            return;
        }
    }
    // No slot expects this browser: it belongs to a tab that was torn off or
    // removed before creation finished. Park it closed instead of leaking a
    // 10x10 orphan window on the old host.
    browser->GetHost()->CloseBrowser(false);
}

void CefShell::on_closed(int browser_id) {
    int index = index_by_id(browser_id);
    if (index < 0) return;
    bool was_active = index == active_;
    const int closed_ws = tabs_[static_cast<size_t>(index)].workspace;
    tabs_.erase(tabs_.begin() + index);
    if (tabs_.empty()) {
        active_ = -1;
        save_session_now();
        DestroyWindow(hwnd_);
        return;
    }
    if (was_active) {
        int target = -1;
        for (int g = index; g < static_cast<int>(tabs_.size()); ++g) {
            if (tabs_[static_cast<size_t>(g)].workspace == closed_ws) {
                target = g;
                break;
            }
        }
        if (target < 0) {
            for (int g = index - 1; g >= 0; --g) {
                if (tabs_[static_cast<size_t>(g)].workspace == closed_ws) {
                    target = g;
                    break;
                }
            }
        }
        if (target >= 0) {
            activate_index(target);
        } else {
            active_ = -1;
            layout();
            update_omnibox();
            update_window_title();
        }
        return;
    }
    if (active_ > index) --active_;
    layout();
    if (find_visible_) refresh_find_bar();
}

void CefShell::on_title(int browser_id, const std::string& title) {
    WinTab* tab = tab_by_id(browser_id);
    if (!tab) return;
    tab->title = title;
    int index = index_by_id(browser_id);
    if (index == active_) update_window_title();
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void CefShell::on_address(int browser_id, const std::string& url) {
    WinTab* tab = tab_by_id(browser_id);
    if (!tab) return;
    tab->url = url;
    update_favicon_host(*tab);
    int index = index_by_id(browser_id);
    if (index == active_) {
        update_omnibox();
        Runtime* rt = runtime();
        star_active_ = rt && rt->bookmarks && !url.empty()
            && !is_start_page_entry(url) && rt->bookmarks->contains(url);
        InvalidateRect(hwnd_, &lay_.tool_btns[3], FALSE);
    }
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void CefShell::on_loading(int browser_id, bool loading) {
    set_tab_loading(browser_id, loading);
    InvalidateRect(hwnd_, nullptr, FALSE);
    if (loading) return;
    // Load finished: record a history visit (never for private tabs, the
    // start page, or when history is disabled by the privacy policy).
    WinTab* tab = tab_by_id(browser_id);
    Runtime* rt = runtime();
    if (!tab || !rt || !rt->history) return;
    if (!rt->settings.history_enabled || tab->private_tab) return;
    if (tab->url.empty() || is_start_page_entry(tab->url)) return;
    rt->history->record(tab->url, tab->title, std::time(nullptr));
}

void CefShell::on_status(int browser_id, const std::string& text) {
    (void)browser_id;
    set_status_text(text);
}

void CefShell::on_popup(int browser_id, const std::string& url, bool private_tab) {
    WinTab* source = tab_by_id(browser_id);
    int ws = source ? source->workspace : active_ws_;
    int index = add_tab(url, private_tab, ws);
    activate_index(index);
}

void CefShell::on_open_url(int browser_id, const std::string& url, bool background) {
    WinTab* source = tab_by_id(browser_id);
    bool private_tab = source ? source->private_tab : false;
    int ws = source ? source->workspace : active_ws_;
    if (url.empty()) return;
    int index = add_tab(url, private_tab, ws);
    if (!background) activate_index(index);
}

void CefShell::on_favicon_urls(int browser_id,
                               const std::vector<std::string>& urls) {
    WinTab* tab = tab_by_id(browser_id);
    if (!tab || tab->private_tab || urls.empty()) return;
    update_favicon_host(*tab);
    if (tab->favicon_host.empty()) return;
    favicons::update_urls(tab->favicon_host, urls);
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
                set_status_text("Private tabs are disabled in settings");
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
        case HK_BOOKMARKS_WINDOW:
            open_bookmarks_manager(hwnd_, [this](const std::string& url) {
                open_in_new_tab(url);
            });
            break;
        case HK_HISTORY_WINDOW:
            open_history_window(hwnd_, [this](const std::string& url) {
                open_in_new_tab(url);
            });
            break;
        case HK_SETTINGS:
            open_settings_window(hwnd_, []() {
                // Theme, style, backdrop and rail side concern every shell
                // window (tear-off made the registry multi-window).
                for (CefShell* window : shell_windows()) {
                    window->reload_style();
                }
            });
            break;
        case HK_BACK: {
            if (CefRefPtr<CefBrowser> browser = active_browser()) browser->GoBack();
            break;
        }
        case HK_FORWARD: {
            if (CefRefPtr<CefBrowser> browser = active_browser()) browser->GoForward();
            break;
        }
        case HK_WS_1: case HK_WS_2: case HK_WS_3: case HK_WS_4:
        case HK_WS_5: case HK_WS_6: case HK_WS_7: case HK_WS_8: {
            int index = key - HK_WS_1;
            if (index >= 0 && index < static_cast<int>(workspaces_.size())) {
                switch_workspace(workspaces_[static_cast<size_t>(index)].id);
            }
            break;
        }
        case HK_NONE:
        default:
            break;
    }
}

void sync_workspaces() {
    EnumWindows([](HWND hwnd, LPARAM) -> BOOL {
        wchar_t cls[32];
        if (GetClassNameW(hwnd, cls, 32) && wcscmp(cls, kWindowClass) == 0) {
            PostMessageW(hwnd, WM_APP_SYNC_WS, 0, 0);
        }
        return TRUE;
    }, 0);
}

int cef_shell_main(bool self_test) {
    static CefShell shell;
    g_shell = &shell;
    shell.set_primary(true);
    shell.set_self_test(self_test);
    int result = shell.exec();
    g_shell = nullptr;
    return result;
}

int cef_self_test_exit_code() {
    return g_self_test_exit;
}

HWND shell_main_hwnd() {
    return g_shell ? g_shell->main_hwnd() : nullptr;
}

void shell_open_url(const std::string& url) {
    if (g_shell) g_shell->open_in_new_tab(url);
}

}




