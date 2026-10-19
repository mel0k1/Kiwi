#include "search_engines_win.hpp"

#include "../cef_runtime.hpp"
#include "aux_theme_win.hpp"

#include <commctrl.h>
#include <windowsx.h>

#include <string>

namespace kiwi {

namespace {

constexpr wchar_t kDialogClass[] = L"KiwiSearchEngines";

enum DialogControl {
    IDC_ENGINES_LIST = 3401,
    IDC_KEYWORD_EDIT = 3402,
    IDC_NAME_EDIT = 3403,
    IDC_TEMPLATE_EDIT = 3404,
    IDC_KEYWORD_LABEL = 3405,
    IDC_NAME_LABEL = 3406,
    IDC_TEMPLATE_LABEL = 3407,
    IDC_SAVE_BTN = 3408,
    IDC_REMOVE_BTN = 3409,
    IDC_DEFAULT_BTN = 3410,
    IDC_CLOSE_BTN = 3411,
    IDC_HINT_LABEL = 3412
};

struct EnginesUi {
    HWND owner = nullptr;
    HWND window = nullptr;
    HWND list = nullptr;
    HWND keyword_edit = nullptr;
    HWND name_edit = nullptr;
    HWND template_edit = nullptr;
    HWND hint = nullptr;
    HFONT font = nullptr;
    bool suspended = false;  // suppress LVN_ITEMCHANGED while repopulating
};

EnginesUi* ui_from_hwnd(HWND hwnd) {
    return reinterpret_cast<EnginesUi*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
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

int scale(HWND hwnd, int value) {
    return MulDiv(value, window_dpi(hwnd), 96);
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

std::string narrow(const std::wstring& text) {
    if (text.empty()) return std::string();
    int size = WideCharToMultiByte(CP_UTF8, 0, text.c_str(),
        static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
    std::string out(static_cast<size_t>(size), '\0');
    if (size > 0) {
        WideCharToMultiByte(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()),
            out.data(), size, nullptr, nullptr);
        while (!out.empty() && out.back() == '\0') out.pop_back();
    }
    return out;
}

std::string edit_text(HWND edit) {
    int length = GetWindowTextLengthW(edit);
    std::wstring text(static_cast<size_t>(length) + 1, L'\0');
    GetWindowTextW(edit, text.data(), length + 1);
    text.resize(static_cast<size_t>(length));
    return narrow(text);
}

std::string trim_copy(const std::string& s) {
    const char* ws = " \t\r\n";
    size_t begin = s.find_first_not_of(ws);
    if (begin == std::string::npos) return std::string();
    size_t end = s.find_last_not_of(ws);
    return s.substr(begin, end - begin + 1);
}

bool selected_keyword(EnginesUi& ui, std::string& keyword) {
    int selected = ListView_GetNextItem(ui.list, -1, LVNI_SELECTED);
    if (selected < 0) return false;
    wchar_t buffer[256] = {};
    LVITEMW item = {};
    item.mask = LVIF_TEXT;
    item.iItem = selected;
    item.pszText = buffer;
    item.cchTextMax = 256;
    if (!SendMessageW(ui.list, LVM_GETITEMW, 0, reinterpret_cast<LPARAM>(&item))) {
        return false;
    }
    keyword = narrow(buffer);
    return true;
}

// Row 0 shows the default engine; rows hold the raw keyword so list text is
// decoupled from the store. lParam carries the store index.
void repopulate(EnginesUi& ui) {
    Runtime* rt = runtime();
    if (!rt) return;
    ui.suspended = true;
    ListView_DeleteAllItems(ui.list);
    const std::vector<mustela::SearchEngine>& engines = rt->settings.search_engines;
    for (size_t i = 0; i < engines.size(); ++i) {
        const mustela::SearchEngine& engine = engines[i];
        bool is_default = !rt->settings.search_default.empty()
            ? rt->settings.search_default == engine.keyword
            : i == 0;
        std::wstring mark = is_default ? L"\u2605 default" : L"";
        std::wstring keyword = wide(engine.keyword);
        std::wstring name = wide(engine.name);
        std::wstring templ = wide(engine.url_template);
        LVITEMW item = {};
        item.mask = LVIF_TEXT | LVIF_PARAM;
        item.iItem = static_cast<int>(i);
        item.pszText = const_cast<LPWSTR>(mark.c_str());
        item.lParam = static_cast<LPARAM>(i);
        int index = ListView_InsertItem(ui.list, &item);
        ListView_SetItemText(ui.list, index, 1,
            const_cast<LPWSTR>(keyword.c_str()));
        ListView_SetItemText(ui.list, index, 2,
            const_cast<LPWSTR>(name.c_str()));
        ListView_SetItemText(ui.list, index, 3,
            const_cast<LPWSTR>(templ.c_str()));
    }
    ui.suspended = false;
}

void load_selected(EnginesUi& ui) {
    Runtime* rt = runtime();
    std::string keyword;
    if (!rt || !selected_keyword(ui, keyword)) return;
    for (const mustela::SearchEngine& engine : rt->settings.search_engines) {
        if (engine.keyword != keyword) continue;
        SetWindowTextW(ui.keyword_edit, wide(engine.keyword).c_str());
        SetWindowTextW(ui.name_edit, wide(engine.name).c_str());
        SetWindowTextW(ui.template_edit, wide(engine.url_template).c_str());
        return;
    }
}

void save_current(EnginesUi& ui) {
    Runtime* rt = runtime();
    if (!rt) return;

    std::string keyword = trim_copy(edit_text(ui.keyword_edit));
    if (!keyword.empty()) {
        for (char& c : keyword) {
            if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
        }
    }
    std::string name = trim_copy(edit_text(ui.name_edit));
    std::string templ = trim_copy(edit_text(ui.template_edit));

    if (keyword.empty() || keyword.find(' ') != std::string::npos) {
        SetWindowTextW(ui.hint, L"Keyword is required and must be one word.");
        return;
    }
    if (templ.empty() || templ.find("%s") == std::string::npos) {
        SetWindowTextW(ui.hint,
            L"Template must contain %s where the query goes.");
        return;
    }

    // Editing an existing engine under a new keyword: drop the old record.
    std::string old_keyword;
    if (selected_keyword(ui, old_keyword) && old_keyword != keyword) {
        for (auto it = rt->settings.search_engines.begin();
             it != rt->settings.search_engines.end(); ++it) {
            if (it->keyword == old_keyword) {
                rt->settings.search_engines.erase(it);
                break;
            }
        }
        if (rt->settings.search_default == old_keyword) {
            rt->settings.search_default = keyword;
        }
    }

    mustela::SearchEngine engine;
    engine.keyword = keyword;
    engine.name = name.empty() ? keyword : name;
    engine.url_template = templ;
    bool replaced = false;
    for (mustela::SearchEngine& existing : rt->settings.search_engines) {
        if (existing.keyword == keyword) {
            existing = engine;
            replaced = true;
            break;
        }
    }
    if (!replaced) rt->settings.search_engines.push_back(engine);

    mustela::save_settings(rt->settings_path, rt->settings);
    repopulate(ui);
    SetWindowTextW(ui.hint, replaced ? L"Engine updated"
                                     : L"Engine added (!keyword in the omnibox)");
}

void remove_selected(EnginesUi& ui) {
    Runtime* rt = runtime();
    std::string keyword;
    if (!rt || !selected_keyword(ui, keyword)) return;
    for (auto it = rt->settings.search_engines.begin();
         it != rt->settings.search_engines.end(); ++it) {
        if (it->keyword == keyword) {
            rt->settings.search_engines.erase(it);
            break;
        }
    }
    if (rt->settings.search_default == keyword) {
        rt->settings.search_default.clear();
    }
    mustela::save_settings(rt->settings_path, rt->settings);
    repopulate(ui);
    SetWindowTextW(ui.hint, L"Engine removed");
}

void set_default_selected(EnginesUi& ui) {
    Runtime* rt = runtime();
    std::string keyword;
    if (!rt || !selected_keyword(ui, keyword)) {
        SetWindowTextW(ui.hint, L"Select an engine first.");
        return;
    }
    rt->settings.search_default = keyword;
    mustela::save_settings(rt->settings_path, rt->settings);
    repopulate(ui);
    SetWindowTextW(ui.hint, L"Default engine updated");
}

void layout_dialog(EnginesUi& ui) {
    if (!ui.window) return;
    RECT client;
    GetClientRect(ui.window, &client);
    const int width = client.right - client.left;
    const int height = client.bottom - client.top;
    const int m = scale(ui.window, 12);
    const int gap = scale(ui.window, 8);
    const int row_h = scale(ui.window, 24);
    const int btn_w = scale(ui.window, 110);
    const int label_w = scale(ui.window, 70);

    int list_h = height - m * 2 - row_h * 4 - gap * 4;
    if (list_h < scale(ui.window, 90)) list_h = scale(ui.window, 90);
    MoveWindow(ui.list, m, m, width - m * 2, list_h, TRUE);

    int y = m + list_h + gap;
    MoveWindow(GetDlgItem(ui.window, IDC_KEYWORD_LABEL), m, y + 4, label_w, row_h, TRUE);
    MoveWindow(ui.keyword_edit, m + label_w + gap, y,
        (width - m * 2 - label_w - gap * 2) / 2, row_h, TRUE);
    MoveWindow(GetDlgItem(ui.window, IDC_NAME_LABEL),
        m + label_w + gap + (width - m * 2 - label_w - gap * 2) / 2 + gap,
        y + 4, label_w, row_h, TRUE);
    MoveWindow(ui.name_edit,
        m + label_w + gap + (width - m * 2 - label_w - gap * 2) / 2 + gap
            + label_w + gap,
        y, width - m * 2 - label_w * 2 - gap * 4, row_h, TRUE);

    y += row_h + gap;
    MoveWindow(GetDlgItem(ui.window, IDC_TEMPLATE_LABEL), m, y + 4, label_w, row_h, TRUE);
    MoveWindow(ui.template_edit, m + label_w + gap, y,
        width - m * 2 - label_w - gap, row_h, TRUE);

    y += row_h + gap;
    MoveWindow(ui.hint, m, y + 4, width - m * 2 - (btn_w + gap) * 4, row_h, TRUE);
    int x = width - m - (btn_w + gap) * 4;
    MoveWindow(GetDlgItem(ui.window, IDC_SAVE_BTN), x, y, btn_w, row_h, TRUE);
    x += btn_w + gap;
    MoveWindow(GetDlgItem(ui.window, IDC_DEFAULT_BTN), x, y, btn_w, row_h, TRUE);
    x += btn_w + gap;
    MoveWindow(GetDlgItem(ui.window, IDC_REMOVE_BTN), x, y, btn_w, row_h, TRUE);
    x += btn_w + gap;
    MoveWindow(GetDlgItem(ui.window, IDC_CLOSE_BTN), x, y, btn_w, row_h, TRUE);
}

LRESULT CALLBACK engines_proc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
    EnginesUi* ui = nullptr;
    if (msg == WM_NCCREATE) {
        auto* create = reinterpret_cast<CREATESTRUCTW*>(lparam);
        ui = static_cast<EnginesUi*>(create->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(ui));
    } else {
        ui = ui_from_hwnd(hwnd);
    }

    switch (msg) {
        case WM_SIZE:
            if (ui) layout_dialog(*ui);
            return 0;
        case WM_ERASEBKGND:
            return 1;
        case WM_CTLCOLOREDIT:
        case WM_CTLCOLORLISTBOX:
            return aux_field_ctlcolor(reinterpret_cast<HDC>(wparam));
        case WM_CTLCOLORSTATIC:
            return aux_muted_ctlcolor(reinterpret_cast<HDC>(wparam));
        case WM_NOTIFY: {
            auto* header = reinterpret_cast<NMHDR*>(lparam);
            if (header->idFrom == IDC_ENGINES_LIST && ui && !ui->suspended) {
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
                case IDC_DEFAULT_BTN:
                    set_default_selected(*ui);
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
        {L"Default", 90},
        {L"Keyword", 110},
        {L"Name", 140},
        {L"URL template", 200}
    };
    LVCOLUMNW column = {};
    column.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_SUBITEM;
    for (int i = 0; i < 4; ++i) {
        column.iSubItem = i;
        column.pszText = const_cast<LPWSTR>(columns[i].title);
        column.cx = scale(list, columns[i].width);
        ListView_InsertColumn(list, i, &column);
    }
    ListView_SetExtendedListViewStyle(list, LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES);
}

}  // namespace

void open_search_engines_window(HWND owner) {
    Runtime* rt = runtime();
    if (!rt) return;
    if (owner) EnableWindow(owner, FALSE);

    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = engines_proc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.hCursor = LoadCursorW(nullptr, reinterpret_cast<LPCWSTR>(IDC_ARROW));
    wc.hbrBackground = gfx::aux_surface_brush();
    wc.lpszClassName = kDialogClass;
    RegisterClassExW(&wc);

    auto* ui = new EnginesUi();
    ui->owner = owner;

    const int dpi = window_dpi(owner);
    const int width = MulDiv(620, dpi > 0 ? dpi : 96, 96);
    const int height = MulDiv(360, dpi > 0 ? dpi : 96, 96);
    RECT area = {0, 0, width, height};
    AdjustWindowRect(&area, WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
        FALSE);
    ui->window = CreateWindowExW(0, kDialogClass, L"Kiwi \u2014 search engines",
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
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | LVS_REPORT | LVS_SINGLESEL
            | LVS_SHOWSELALWAYS,
        0, 0, 10, 10, ui->window,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_ENGINES_LIST)),
        GetModuleHandleW(nullptr), nullptr);
    init_list_columns(ui->list);
    apply_list_theme(ui->list);

    auto make_label = [&ui](const wchar_t* text, DialogControl id) {
        return CreateWindowExW(0, L"STATIC", text, WS_CHILD | WS_VISIBLE,
            0, 0, 10, 10, ui->window,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
            GetModuleHandleW(nullptr), nullptr);
    };
    make_label(L"Keyword:", IDC_KEYWORD_LABEL);
    make_label(L"Name:", IDC_NAME_LABEL);
    make_label(L"Template:", IDC_TEMPLATE_LABEL);

    auto make_edit = [&ui](DialogControl id, const wchar_t* cue) {
        HWND edit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", nullptr,
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
            0, 0, 10, 10, ui->window,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
            GetModuleHandleW(nullptr), nullptr);
        if (cue) {
            SendMessageW(edit, EM_SETCUEBANNER, TRUE,
                reinterpret_cast<LPARAM>(cue));
        }
        return edit;
    };
    ui->keyword_edit = make_edit(IDC_KEYWORD_EDIT, L"ddg");
    ui->name_edit = make_edit(IDC_NAME_EDIT, L"DuckDuckGo");
    ui->template_edit = make_edit(IDC_TEMPLATE_EDIT,
        L"https://duckduckgo.com/?q=%s");

    auto make_button = [&ui](const wchar_t* text, DialogControl id) {
        return CreateWindowExW(0, L"BUTTON", text,
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
            0, 0, 10, 10, ui->window,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
            GetModuleHandleW(nullptr), nullptr);
    };
    make_button(L"Save engine", IDC_SAVE_BTN);
    make_button(L"Set as default", IDC_DEFAULT_BTN);
    make_button(L"Remove", IDC_REMOVE_BTN);
    make_button(L"Close", IDC_CLOSE_BTN);

    ui->hint = CreateWindowExW(0, L"STATIC",
        L"Select an engine to edit it; !keyword searches from the omnibox.",
        WS_CHILD | WS_VISIBLE, 0, 0, 10, 10, ui->window,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_HINT_LABEL)),
        GetModuleHandleW(nullptr), nullptr);

    SendMessageW(ui->list, WM_SETFONT, reinterpret_cast<WPARAM>(ui->font), TRUE);
    SendMessageW(ui->keyword_edit, WM_SETFONT, reinterpret_cast<WPARAM>(ui->font), TRUE);
    SendMessageW(ui->name_edit, WM_SETFONT, reinterpret_cast<WPARAM>(ui->font), TRUE);
    SendMessageW(ui->template_edit, WM_SETFONT, reinterpret_cast<WPARAM>(ui->font), TRUE);
    SendMessageW(ui->hint, WM_SETFONT, reinterpret_cast<WPARAM>(ui->font), TRUE);
    for (DialogControl id :
         {IDC_SAVE_BTN, IDC_DEFAULT_BTN, IDC_REMOVE_BTN, IDC_CLOSE_BTN,
          IDC_KEYWORD_LABEL, IDC_NAME_LABEL, IDC_TEMPLATE_LABEL}) {
        HWND ctrl = GetDlgItem(ui->window, id);
        SendMessageW(ctrl, WM_SETFONT, reinterpret_cast<WPARAM>(ui->font), TRUE);
        apply_control_theme(ctrl);
    }

    repopulate(*ui);
    layout_dialog(*ui);
    ShowWindow(ui->window, SW_SHOW);
    UpdateWindow(ui->window);
    SetFocus(ui->list);
}

}
