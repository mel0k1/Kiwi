#include "settings_win.hpp"

#include "../cef_runtime.hpp"
#include "cef_shell_win.hpp"
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

#include <functional>
#include <string>

namespace kiwi {

namespace {

constexpr wchar_t kClass[] = L"KiwiSettingsWindow";
constexpr COLORREF kSurface = RGB(255, 245, 240);
constexpr COLORREF kInk = RGB(44, 62, 80);
constexpr COLORREF kMuted = RGB(133, 146, 158);

enum {
    ID_STYLE = 3001,
    ID_HOMEPAGE,
    ID_SEARCH,
    ID_DOH,
    ID_COOKIES,
    ID_JAVASCRIPT,
    ID_AUTOPLAY,
    ID_GPC,
    ID_AUDITING,
    ID_FP_WEBGL,
    ID_FP_CANVAS,
    ID_PRIVATE,
    ID_SITERULES,
    ID_HISTORY,
    ID_UPDATE,
    ID_SAVE,
    ID_CLOSEWIN,
};

struct SettingsState {
    HWND hwnd = nullptr;
    std::function<void()> on_style_changed;
    std::string original_style;
};

SettingsState* state_for(HWND hwnd) {
    return reinterpret_cast<SettingsState*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
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
        WS_CHILD | WS_VISIBLE | style, x, y, w, h, parent,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(
            reinterpret_cast<uintptr_t>(id))),
        GetModuleHandleW(nullptr), nullptr);
    SendMessageW(ctrl, WM_SETFONT, (WPARAM)font, TRUE);
    return ctrl;
}

int checkbox_value(HWND hwnd, int id) {
    return SendMessageW(GetDlgItem(hwnd, id), BM_GETCHECK, 0, 0) == BST_CHECKED ? 1 : 0;
}

void set_checkbox(HWND hwnd, int id, bool value) {
    SendMessageW(GetDlgItem(hwnd, id), BM_SETCHECK,
        value ? BST_CHECKED : BST_UNCHECKED, 0);
}

void load_into_controls(SettingsState* st) {
    Runtime* rt = runtime();
    if (!rt) return;
    const mustela::Settings& s = rt->settings;

    SendMessageW(GetDlgItem(st->hwnd, ID_STYLE), CB_RESETCONTENT, 0, 0);
    SendMessageW(GetDlgItem(st->hwnd, ID_STYLE), CB_ADDSTRING, 0,
        reinterpret_cast<LPARAM>(L"Classic"));
    SendMessageW(GetDlgItem(st->hwnd, ID_STYLE), CB_ADDSTRING, 0,
        reinterpret_cast<LPARAM>(L"Zen"));
    SendMessageW(GetDlgItem(st->hwnd, ID_STYLE), CB_SETCURSEL,
        st->original_style == "zen" ? 1 : 0, 0);

    set_edit_utf8(GetDlgItem(st->hwnd, ID_HOMEPAGE), s.homepage);
    set_edit_utf8(GetDlgItem(st->hwnd, ID_SEARCH), s.search_template);
    set_edit_utf8(GetDlgItem(st->hwnd, ID_DOH), s.dns_over_https);

    HWND cookies = GetDlgItem(st->hwnd, ID_COOKIES);
    SendMessageW(cookies, CB_RESETCONTENT, 0, 0);
    SendMessageW(cookies, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Allow all"));
    SendMessageW(cookies, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Block third-party"));
    SendMessageW(cookies, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Block all"));
    SendMessageW(cookies, CB_SETCURSEL,
        s.cookie_policy == "always" ? 0
            : (s.cookie_policy == "never" ? 2 : 1), 0);

    set_checkbox(st->hwnd, ID_JAVASCRIPT, s.javascript_enabled);
    set_checkbox(st->hwnd, ID_AUTOPLAY, s.block_autoplay);
    set_checkbox(st->hwnd, ID_GPC, s.gpc);
    set_checkbox(st->hwnd, ID_AUDITING, s.hyperlink_auditing);
    set_checkbox(st->hwnd, ID_FP_WEBGL, s.fingerprint_webgl);
    set_checkbox(st->hwnd, ID_FP_CANVAS, s.fingerprint_canvas);
    set_checkbox(st->hwnd, ID_PRIVATE, s.private_tabs_enabled);
}

bool save_from_controls(SettingsState* st) {
    Runtime* rt = runtime();
    if (!rt) return false;
    mustela::Settings& s = rt->settings;

    int style_sel = SendMessageW(GetDlgItem(st->hwnd, ID_STYLE), CB_GETCURSEL, 0, 0);
    std::string style = style_sel == 1 ? "zen" : "classic";
    bool style_changed = style != st->original_style;
    kiwi::save_shell_style(rt->config_dir, style);

    s.homepage = edit_text(GetDlgItem(st->hwnd, ID_HOMEPAGE));
    s.search_template = edit_text(GetDlgItem(st->hwnd, ID_SEARCH));
    s.dns_over_https = edit_text(GetDlgItem(st->hwnd, ID_DOH));

    int cookies = SendMessageW(GetDlgItem(st->hwnd, ID_COOKIES), CB_GETCURSEL, 0, 0);
    s.cookie_policy = cookies == 0 ? "always" : (cookies == 2 ? "never"
                                                              : "no-third-party");

    s.javascript_enabled = checkbox_value(st->hwnd, ID_JAVASCRIPT) != 0;
    s.block_autoplay = checkbox_value(st->hwnd, ID_AUTOPLAY) != 0;
    s.gpc = checkbox_value(st->hwnd, ID_GPC) != 0;
    s.hyperlink_auditing = checkbox_value(st->hwnd, ID_AUDITING) != 0;
    s.fingerprint_webgl = checkbox_value(st->hwnd, ID_FP_WEBGL) != 0;
    s.fingerprint_canvas = checkbox_value(st->hwnd, ID_FP_CANVAS) != 0;
    s.private_tabs_enabled = checkbox_value(st->hwnd, ID_PRIVATE) != 0;

    mustela::save_settings(rt->settings_path, s);
    rt->policy = mustela::privacy_from(s);
    rt->policy.private_tabs_available = s.private_tabs_enabled;
    rt->blocklist = mustela::default_blocklist(
        rt->policy.blocklist ? s.blocklist_file : std::string());

    st->original_style = style;
    if (style_changed && st->on_style_changed) st->on_style_changed();
    return true;
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
                st->hwnd = nullptr;
                delete st;
                SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
            }
            return 0;
        }
        default:
            break;
    }
    SettingsState* st = state_for(hwnd);
    if (!st) return DefWindowProcW(hwnd, msg, wparam, lparam);

    switch (msg) {
        case WM_COMMAND: {
            int id = LOWORD(wparam);
            if (id == ID_SAVE) {
                save_from_controls(st);
                MessageBoxW(hwnd, L"Settings saved.", L"Kiwi",
                    MB_OK | MB_ICONINFORMATION);
                return 0;
            }
            if (id == ID_CLOSEWIN) {
                DestroyWindow(hwnd);
                return 0;
            }
            if (id == ID_SITERULES) {
                open_site_rules_dialog(st->hwnd);
                return 0;
            }
            if (id == ID_HISTORY) {
                Runtime* rt = runtime();
                if (rt) {
                    // The history window lives in the shell; opening it from
                    // settings detaches ownership by asking the shell via its
                    // owned-window lookup (same top-level owner).
                    open_history_window(shell_main_hwnd(),
                        [](const std::string& url) { shell_open_url(url); });
                }
                return 0;
            }
            if (id == ID_UPDATE) {
                check_for_updates(hwnd);
                return 0;
            }
            return 0;
        }
        default:
            break;
    }
    return DefWindowProcW(hwnd, msg, wparam, lparam);
}

}  // namespace

void open_settings_window(HWND owner, std::function<void()> on_style_changed) {
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

    RECT area = {0, 0, 470, 520};
    AdjustWindowRect(&area, WS_OVERLAPPEDWINDOW, FALSE);
    st->hwnd = CreateWindowExW(0, kClass, L"Kiwi settings",
        WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT,
        area.right - area.left, area.bottom - area.top,
        owner, nullptr, GetModuleHandleW(nullptr), st);
    if (!st->hwnd) {
        delete st;
        return;
    }

    HFONT font = CreateFontW(-MulDiv(10, 96, 72), 0, 0, 0, FW_NORMAL, FALSE, FALSE,
        FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
    HFONT bold = CreateFontW(-MulDiv(10, 96, 72), 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE,
        FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
    auto label = [&](const wchar_t* text, int x, int y, int w) {
        make_control(st->hwnd, L"STATIC", text, 0, nullptr, x, y, w, 18, font);
    };
    auto heading = [&](const wchar_t* text, int x, int y) {
        make_control(st->hwnd, L"STATIC", text, 0, nullptr, x, y, 300, 20, bold);
    };
    auto edit = [&](HMENU id, int x, int y, int w) {
        make_control(st->hwnd, L"EDIT", nullptr, ES_AUTOHSCROLL, id, x, y, w, 24, font);
    };
    auto combo = [&](HMENU id, int x, int y, int w) {
        make_control(st->hwnd, L"COMBOBOX", nullptr,
            CBS_DROPDOWNLIST | WS_TABSTOP, id, x, y, w, 200, font);
    };
    auto check = [&](HMENU id, const wchar_t* text, int x, int y) {
        make_control(st->hwnd, L"BUTTON", text, BS_AUTOCHECKBOX | BS_TEXT | WS_TABSTOP,
            id, x, y, 260, 18, font);
    };

    int x0 = 14;
    int w = 470 - x0 * 2;
    int y = 14;
    heading(L"Appearance", x0, y); y += 24;
    label(L"Shell style", x0, y + 3, 120);
    combo((HMENU)(intptr_t)ID_STYLE, x0 + 130, y, w - 130); y += 30;

    heading(L"Search and home", x0, y + 4); y += 28;
    label(L"Homepage", x0, y + 3, 120);
    edit((HMENU)(intptr_t)ID_HOMEPAGE, x0 + 130, y, w - 130); y += 28;
    label(L"Search template (%s)", x0, y + 3, 120);
    edit((HMENU)(intptr_t)ID_SEARCH, x0 + 130, y, w - 130); y += 30;

    heading(L"Privacy", x0, y + 4); y += 28;
    label(L"Cookie policy", x0, y + 3, 120);
    combo((HMENU)(intptr_t)ID_COOKIES, x0 + 130, y, w - 130); y += 30;
    label(L"DNS over HTTPS", x0, y + 3, 120);
    edit((HMENU)(intptr_t)ID_DOH, x0 + 130, y, w - 130); y += 28;
    check((HMENU)(intptr_t)ID_JAVASCRIPT, L"Enable JavaScript", x0, y); y += 22;
    check((HMENU)(intptr_t)ID_AUTOPLAY, L"Block autoplay (require user gesture)", x0, y); y += 22;
    check((HMENU)(intptr_t)ID_GPC, L"Global Privacy Control (Sec-GPC)", x0, y); y += 22;
    check((HMENU)(intptr_t)ID_AUDITING, L"Allow hyperlink auditing (<a ping>)", x0, y); y += 22;
    check((HMENU)(intptr_t)ID_FP_WEBGL, L"Fingerprint protection: WebGL vendor", x0, y); y += 22;
    check((HMENU)(intptr_t)ID_FP_CANVAS, L"Fingerprint protection: canvas", x0, y); y += 26;

    heading(L"General", x0, y + 4); y += 28;
    check((HMENU)(intptr_t)ID_PRIVATE, L"Private tabs available (Ctrl+Shift+P)", x0, y); y += 30;

    int bw = 150;
    make_control(st->hwnd, L"BUTTON", L"Per-site rules...",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP, (HMENU)(intptr_t)ID_SITERULES,
        x0, y, bw, 26, font);
    make_control(st->hwnd, L"BUTTON", L"Browsing history...",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP, (HMENU)(intptr_t)ID_HISTORY,
        x0 + bw + 8, y, bw, 26, font);
    make_control(st->hwnd, L"BUTTON", L"Check for updates...",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP, (HMENU)(intptr_t)ID_UPDATE,
        x0 + (bw + 8) * 2, y, bw + 20, 26, font);
    y += 32;
    make_control(st->hwnd, L"BUTTON", L"Save",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP, (HMENU)(intptr_t)ID_SAVE,
        x0, y, bw, 26, font);
    make_control(st->hwnd, L"BUTTON", L"Close",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP, (HMENU)(intptr_t)ID_CLOSEWIN,
        x0 + bw + 8, y, bw, 26, font);

    load_into_controls(st);
    ShowWindow(st->hwnd, SW_SHOW);
    UpdateWindow(st->hwnd);
}

}  // namespace kiwi
