#include "site_rules_win.hpp"

#include "../cef_runtime.hpp"

#include <commctrl.h>
#include <windowsx.h>

#include <string>

namespace kiwi {

namespace {

constexpr wchar_t kDialogClass[] = L"KiwiSiteRules";
constexpr wchar_t kEditorClass[] = L"KiwiSiteRuleEdit";

enum DialogControl {
    IDC_RULES_LIST = 3001,
    IDC_HOST_EDIT = 3002,
    IDC_JS_COMBO = 3003,
    IDC_COOKIE_COMBO = 3004,
    IDC_FP_COMBO = 3005,
    IDC_SAVE_BTN = 3006,
    IDC_REMOVE_BTN = 3007,
    IDC_CLOSE_BTN = 3008,
    IDC_HOST_LABEL = 3009,
    IDC_JS_LABEL = 3010,
    IDC_COOKIE_LABEL = 3011,
    IDC_FP_LABEL = 3012,
    IDC_HINT_LABEL = 3013
};

// The editor window keeps its state here; the shell never touches it.
struct SiteRulesUi {
    HWND owner = nullptr;
    HWND window = nullptr;
    HWND list = nullptr;
    HWND host_edit = nullptr;
    HWND js_combo = nullptr;
    HWND cookie_combo = nullptr;
    HWND fp_combo = nullptr;
    HWND hint = nullptr;
    HFONT font = nullptr;
    bool suspended = false;  // suppress LVN_ITEMCHANGED while repopulating

    int dpi_for_window() const {
        return dpi_for_window_impl(window);
    }

    static int dpi_for_window_impl(HWND hwnd) {
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
};

SiteRulesUi* ui_from_hwnd(HWND hwnd) {
    return reinterpret_cast<SiteRulesUi*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
}

int dpi_scale(HWND hwnd, int value) {
    return MulDiv(value, SiteRulesUi::dpi_for_window_impl(hwnd), 96);
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

std::string narrow(const std::wstring& wide_text) {
    if (wide_text.empty()) return std::string();
    int size = WideCharToMultiByte(CP_UTF8, 0, wide_text.c_str(),
        static_cast<int>(wide_text.size()), nullptr, 0, nullptr, nullptr);
    std::string out(static_cast<size_t>(size), '\0');
    if (size > 0) {
        WideCharToMultiByte(CP_UTF8, 0, wide_text.c_str(),
            static_cast<int>(wide_text.size()), out.data(), size, nullptr, nullptr);
        while (!out.empty() && out.back() == '\0') out.pop_back();
    }
    return out;
}

void fill_rule_combo(HWND combo) {
    ComboBox_ResetContent(combo);
    ComboBox_AddString(combo, L"default");
    ComboBox_AddString(combo, L"allow");
    ComboBox_AddString(combo, L"deny");
    ComboBox_SetCurSel(combo, 0);
}

mustela::SiteRule combo_rule(HWND combo) {
    int index = ComboBox_GetCurSel(combo);
    if (index == 1) return mustela::SiteRule::Allow;
    if (index == 2) return mustela::SiteRule::Deny;
    return mustela::SiteRule::Default;
}

void set_combo_rule(HWND combo, mustela::SiteRule rule) {
    int index = 0;
    if (rule == mustela::SiteRule::Allow) index = 1;
    if (rule == mustela::SiteRule::Deny) index = 2;
    ComboBox_SetCurSel(combo, index);
}

void repopulate(SiteRulesUi& ui) {
    ui.suspended = true;
    ListView_DeleteAllItems(ui.list);
    Runtime* rt = runtime();
    if (rt && rt->site_rules) {
        int row = 0;
        for (const mustela::SiteRules::Entry& entry : rt->site_rules->entries()) {
            const std::wstring host_text = wide(entry.host);
            LVITEMW item = {};
            item.mask = LVIF_TEXT | LVIF_PARAM;
            item.iItem = row;
            item.pszText = const_cast<LPWSTR>(host_text.c_str());
            item.lParam = row;
            int index = ListView_InsertItem(ui.list, &item);
            const mustela::SiteRule rules[3] = {
                entry.policy.javascript, entry.policy.cookies, entry.policy.fingerprint
            };
            for (int col = 0; col < 3; ++col) {
                ListView_SetItemText(ui.list, index, col + 1,
                    const_cast<LPWSTR>(wide(mustela::site_rule_name(rules[col])).c_str()));
            }
            ++row;
        }
    }
    ui.suspended = false;
}

int selected_index(HWND list) {
    return ListView_GetNextItem(list, -1, LVNI_SELECTED);
}

std::string trimmed(std::string host) {
    const char* ws = " \t\r\n";
    size_t begin = host.find_first_not_of(ws);
    if (begin == std::string::npos) return std::string();
    size_t end = host.find_last_not_of(ws);
    host = host.substr(begin, end - begin + 1);
    for (char& c : host) {
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    }
    // Accept a pasted URL: reduce it to the host part.
    return mustela::host_from_uri(host);
}

void save_current(SiteRulesUi& ui) {
    Runtime* rt = runtime();
    if (!rt || !rt->site_rules) return;

    int length = GetWindowTextLengthW(ui.host_edit);
    std::wstring text(static_cast<size_t>(length) + 1, L'\0');
    GetWindowTextW(ui.host_edit, text.data(), length + 1);
    text.resize(static_cast<size_t>(length));
    const std::string host = trimmed(narrow(text));
    if (host.empty()) {
        SetWindowTextW(ui.hint, L"Enter a site host first, e.g. example.com");
        return;
    }

    mustela::SitePolicy policy;
    // Preserve the fields this dialog does not edit yet.
    if (const mustela::SitePolicy* existing = rt->site_rules->find(host)) {
        policy = *existing;
    }
    policy.javascript = combo_rule(ui.js_combo);
    policy.cookies = combo_rule(ui.cookie_combo);
    policy.fingerprint = combo_rule(ui.fp_combo);

    // Renaming an entry: drop the old record first.
    int selected = selected_index(ui.list);
    if (selected >= 0) {
        wchar_t old_host[256] = {};
        LVITEMW item = {};
        item.mask = LVIF_TEXT;
        item.iItem = selected;
        item.pszText = old_host;
        item.cchTextMax = 256;
        if (SendMessageW(ui.list, LVM_GETITEMW, 0, reinterpret_cast<LPARAM>(&item))
            && host != narrow(old_host)) {
            rt->site_rules->remove(narrow(old_host));
        }
    }

    rt->site_rules->set(host, policy);
    rt->site_rules->save();
    repopulate(ui);
    SetWindowTextW(ui.hint, L"Rule saved");
}

void remove_selected(SiteRulesUi& ui) {
    Runtime* rt = runtime();
    if (!rt || !rt->site_rules) return;
    int selected = selected_index(ui.list);
    if (selected < 0) return;
    wchar_t host[256] = {};
    LVITEMW item = {};
    item.mask = LVIF_TEXT;
    item.iItem = selected;
    item.pszText = host;
    item.cchTextMax = 256;
    if (!SendMessageW(ui.list, LVM_GETITEMW, 0, reinterpret_cast<LPARAM>(&item))) return;
    rt->site_rules->remove(narrow(host));
    rt->site_rules->save();
    repopulate(ui);
    SetWindowTextW(ui.hint, L"Rule removed");
}

void load_selected(SiteRulesUi& ui) {
    Runtime* rt = runtime();
    int selected = selected_index(ui.list);
    if (selected < 0 || !rt || !rt->site_rules) return;
    wchar_t host[256] = {};
    LVITEMW item = {};
    item.mask = LVIF_TEXT;
    item.iItem = selected;
    item.pszText = host;
    item.cchTextMax = 256;
    if (!SendMessageW(ui.list, LVM_GETITEMW, 0, reinterpret_cast<LPARAM>(&item))) return;
    SetWindowTextW(ui.host_edit, host);
    const mustela::SitePolicy* policy = rt->site_rules->find(narrow(host));
    if (policy) {
        set_combo_rule(ui.js_combo, policy->javascript);
        set_combo_rule(ui.cookie_combo, policy->cookies);
        set_combo_rule(ui.fp_combo, policy->fingerprint);
    }
}

void layout_dialog(SiteRulesUi& ui) {
    if (!ui.window) return;
    RECT client;
    GetClientRect(ui.window, &client);
    const int width = client.right - client.left;
    const int height = client.bottom - client.top;
    const int m = dpi_scale(ui.window, 12);   // outer margin
    const int gap = dpi_scale(ui.window, 8);  // inner gap
    const int row_h = dpi_scale(ui.window, 24);
    const int btn_w = dpi_scale(ui.window, 96);

    int list_h = height - m * 2 - row_h * 3 - gap * 4;
    if (list_h < dpi_scale(ui.window, 80)) list_h = dpi_scale(ui.window, 80);
    MoveWindow(ui.list, m, m, width - m * 2, list_h, TRUE);

    int y = m + list_h + gap;
    MoveWindow(GetDlgItem(ui.window, IDC_HOST_LABEL), m, y + 3,
        dpi_scale(ui.window, 64), row_h, TRUE);
    MoveWindow(ui.host_edit, m + dpi_scale(ui.window, 70), y,
        width - m * 2 - dpi_scale(ui.window, 70), row_h, TRUE);

    y += row_h + gap;
    const int col_w = (width - m * 2 - gap * 2 - dpi_scale(ui.window, 64) * 3) / 3;
    int x = m;
    MoveWindow(GetDlgItem(ui.window, IDC_JS_LABEL), x, y + 3,
        dpi_scale(ui.window, 64), row_h, TRUE);
    MoveWindow(ui.js_combo, x + dpi_scale(ui.window, 64), y, col_w, row_h * 8, TRUE);
    x += col_w + gap;
    MoveWindow(GetDlgItem(ui.window, IDC_COOKIE_LABEL), x, y + 3,
        dpi_scale(ui.window, 64), row_h, TRUE);
    MoveWindow(ui.cookie_combo, x + dpi_scale(ui.window, 64), y, col_w, row_h * 8, TRUE);
    x += col_w + gap;
    MoveWindow(GetDlgItem(ui.window, IDC_FP_LABEL), x, y + 3,
        dpi_scale(ui.window, 72), row_h, TRUE);
    MoveWindow(ui.fp_combo, x + dpi_scale(ui.window, 72), y, col_w, row_h * 8, TRUE);

    y += row_h + gap;
    MoveWindow(ui.hint, m, y + 4, width - m * 2 - (btn_w + gap) * 3, row_h, TRUE);
    MoveWindow(GetDlgItem(ui.window, IDC_SAVE_BTN), width - m - (btn_w + gap) * 3,
        y, btn_w, row_h, TRUE);
    MoveWindow(GetDlgItem(ui.window, IDC_REMOVE_BTN),
        width - m - (btn_w + gap) * 2, y, btn_w, row_h, TRUE);
    MoveWindow(GetDlgItem(ui.window, IDC_CLOSE_BTN), width - m - btn_w,
        y, btn_w, row_h, TRUE);
}

LRESULT CALLBACK site_rules_proc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
    SiteRulesUi* ui = nullptr;
    if (msg == WM_NCCREATE) {
        auto* create = reinterpret_cast<CREATESTRUCTW*>(lparam);
        ui = static_cast<SiteRulesUi*>(create->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(ui));
    } else {
        ui = ui_from_hwnd(hwnd);
    }

    switch (msg) {
        case WM_SIZE:
            if (ui) layout_dialog(*ui);
            return 0;
        case WM_ERASEBKGND: {
            HDC dc = reinterpret_cast<HDC>(wparam);
            RECT rect;
            GetClientRect(hwnd, &rect);
            HBRUSH brush = CreateSolidBrush(RGB(255, 245, 240));
            FillRect(dc, &rect, brush);
            DeleteObject(brush);
            return 1;
        }
        case WM_CTLCOLOREDIT: {
            HDC dc = reinterpret_cast<HDC>(wparam);
            SetBkColor(dc, RGB(255, 255, 255));
            SetTextColor(dc, RGB(44, 62, 80));
            static HBRUSH white = CreateSolidBrush(RGB(255, 255, 255));
            return reinterpret_cast<LRESULT>(white);
        }
        case WM_CTLCOLORSTATIC: {
            HDC dc = reinterpret_cast<HDC>(wparam);
            SetBkColor(dc, RGB(255, 245, 240));
            SetTextColor(dc, RGB(133, 146, 158));
            static HBRUSH surface = CreateSolidBrush(RGB(255, 245, 240));
            return reinterpret_cast<LRESULT>(surface);
        }
        case WM_NOTIFY: {
            auto* header = reinterpret_cast<NMHDR*>(lparam);
            if (header->idFrom == IDC_RULES_LIST && ui && !ui->suspended) {
                if (header->code == LVN_ITEMCHANGED) {
                    auto* change = reinterpret_cast<NMLISTVIEW*>(lparam);
                    if ((change->uChanged & LVIF_STATE)
                        && (change->uNewState & LVIS_SELECTED)) {
                        load_selected(*ui);
                    }
                } else if (header->code == NM_DBLCLK) {
                    load_selected(*ui);
                }
            }
            break;
        }
        case WM_COMMAND: {
            if (!ui) break;
            switch (LOWORD(wparam)) {
                case IDC_SAVE_BTN:
                    save_current(*ui);
                    return 0;
                case IDC_REMOVE_BTN:
                    remove_selected(*ui);
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

void init_list_columns(HWND list) {
    struct Column {
        const wchar_t* title;
        int width;
    };
    const Column columns[4] = {
        {L"Site", 190},
        {L"JavaScript", 90},
        {L"Cookies", 90},
        {L"Fingerprint", 96}
    };
    LVCOLUMNW column = {};
    column.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_SUBITEM;
    for (int i = 0; i < 4; ++i) {
        column.iSubItem = i;
        column.pszText = const_cast<LPWSTR>(columns[i].title);
        column.cx = dpi_scale(list, columns[i].width);
        ListView_InsertColumn(list, i, &column);
    }
    ListView_SetExtendedListViewStyle(list, LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES);
}

}  // namespace

void open_site_rules_dialog(HWND owner) {
    Runtime* rt = runtime();
    if (!rt || !rt->site_rules) return;
    if (owner) EnableWindow(owner, FALSE);

    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = site_rules_proc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.hCursor = LoadCursorW(nullptr, reinterpret_cast<LPCWSTR>(IDC_ARROW));
    wc.hbrBackground = CreateSolidBrush(RGB(255, 245, 240));
    wc.lpszClassName = kDialogClass;
    RegisterClassExW(&wc);

    auto* ui = new SiteRulesUi();
    ui->owner = owner;

    const int dpi = SiteRulesUi::dpi_for_window_impl(owner);
    const int width = MulDiv(560, dpi > 0 ? dpi : 96, 96);
    const int height = MulDiv(380, dpi > 0 ? dpi : 96, 96);
    RECT area = {0, 0, width, height};
    AdjustWindowRect(&area, WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
        FALSE);
    ui->window = CreateWindowExW(0, kDialogClass, L"Kiwi \u2014 per-site rules",
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
        CW_USEDEFAULT, CW_USEDEFAULT, area.right - area.left,
        area.bottom - area.top, owner, nullptr, GetModuleHandleW(nullptr), ui);

    if (!ui->window) {
        delete ui;
        if (owner) EnableWindow(owner, TRUE);
        return;
    }
    if (owner) SetWindowLongPtrW(ui->window, GWLP_HWNDPARENT,
        reinterpret_cast<LONG_PTR>(owner));

    ui->font = CreateFontW(-MulDiv(10, dpi > 0 ? dpi : 96, 72), 0, 0, 0, FW_NORMAL,
        FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");

    ui->list = CreateWindowExW(WS_EX_CLIENTEDGE, WC_LISTVIEWW, nullptr,
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | LVS_REPORT | LVS_SINGLESEL | LVS_SHOWSELALWAYS,
        0, 0, 10, 10, ui->window,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_RULES_LIST)),
        GetModuleHandleW(nullptr), nullptr);
    init_list_columns(ui->list);

    auto make_label = [&ui](const wchar_t* text, DialogControl id) {
        return CreateWindowExW(0, L"STATIC", text, WS_CHILD | WS_VISIBLE,
            0, 0, 10, 10, ui->window,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
            GetModuleHandleW(nullptr), nullptr);
    };
    make_label(L"Site:", IDC_HOST_LABEL);
    make_label(L"JavaScript:", IDC_JS_LABEL);
    make_label(L"Cookies:", IDC_COOKIE_LABEL);
    make_label(L"Fingerprint:", IDC_FP_LABEL);

    ui->host_edit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", nullptr,
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
        0, 0, 10, 10, ui->window,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_HOST_EDIT)),
        GetModuleHandleW(nullptr), nullptr);
    SendMessageW(ui->host_edit, EM_SETCUEBANNER, TRUE,
        reinterpret_cast<LPARAM>(L"example.com"));

    ui->js_combo = CreateWindowExW(0, L"COMBOBOX", nullptr,
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | CBS_DROPDOWNLIST,
        0, 0, 10, 200, ui->window,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_JS_COMBO)),
        GetModuleHandleW(nullptr), nullptr);
    ui->cookie_combo = CreateWindowExW(0, L"COMBOBOX", nullptr,
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | CBS_DROPDOWNLIST,
        0, 0, 10, 200, ui->window,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_COOKIE_COMBO)),
        GetModuleHandleW(nullptr), nullptr);
    ui->fp_combo = CreateWindowExW(0, L"COMBOBOX", nullptr,
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | CBS_DROPDOWNLIST,
        0, 0, 10, 200, ui->window,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_FP_COMBO)),
        GetModuleHandleW(nullptr), nullptr);
    fill_rule_combo(ui->js_combo);
    fill_rule_combo(ui->cookie_combo);
    fill_rule_combo(ui->fp_combo);

    auto make_button = [&ui](const wchar_t* text, DialogControl id) {
        return CreateWindowExW(0, L"BUTTON", text,
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
            0, 0, 10, 10, ui->window,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
            GetModuleHandleW(nullptr), nullptr);
    };
    make_button(L"Save rule", IDC_SAVE_BTN);
    make_button(L"Remove", IDC_REMOVE_BTN);
    make_button(L"Close", IDC_CLOSE_BTN);

    ui->hint = CreateWindowExW(0, L"STATIC",
        L"Select a rule to edit it; Save writes sites.toml.",
        WS_CHILD | WS_VISIBLE, 0, 0, 10, 10, ui->window,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_HINT_LABEL)),
        GetModuleHandleW(nullptr), nullptr);

    SendMessageW(ui->list, WM_SETFONT, reinterpret_cast<WPARAM>(ui->font), TRUE);
    SendMessageW(ui->host_edit, WM_SETFONT, reinterpret_cast<WPARAM>(ui->font), TRUE);
    SendMessageW(ui->js_combo, WM_SETFONT, reinterpret_cast<WPARAM>(ui->font), TRUE);
    SendMessageW(ui->cookie_combo, WM_SETFONT, reinterpret_cast<WPARAM>(ui->font), TRUE);
    SendMessageW(ui->fp_combo, WM_SETFONT, reinterpret_cast<WPARAM>(ui->font), TRUE);
    SendMessageW(ui->hint, WM_SETFONT, reinterpret_cast<WPARAM>(ui->font), TRUE);
    for (DialogControl id :
         {IDC_SAVE_BTN, IDC_REMOVE_BTN, IDC_CLOSE_BTN, IDC_HOST_LABEL, IDC_JS_LABEL,
          IDC_COOKIE_LABEL, IDC_FP_LABEL}) {
        SendMessageW(GetDlgItem(ui->window, id), WM_SETFONT,
            reinterpret_cast<WPARAM>(ui->font), TRUE);
    }

    repopulate(*ui);
    layout_dialog(*ui);
    ShowWindow(ui->window, SW_SHOW);
    UpdateWindow(ui->window);
    SetFocus(ui->list);
}

}
