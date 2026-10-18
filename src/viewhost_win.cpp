#include "viewhost.hpp"

#include <windows.h>

#include <memory>
#include <vector>

#include <gdk/gdkwin32.h>

namespace kiwi {

namespace {

constexpr UINT WM_APP_SHORTCUT = WM_APP + 0x4B49;

struct ViewPage {
    mustela::View* view = nullptr;
    GtkWidget* area = nullptr;
    GtkWindow* parent = nullptr;
    bool attached = false;
};

// Pages must live at stable addresses: GTK signal handlers hold raw ViewPage
// pointers for the whole life of the drawing area. A plain vector would
// reallocate on every new tab and turn every earlier pointer into garbage
// (the first use-after-free hit the very next map/resize of an old tab).
std::vector<std::unique_ptr<ViewPage>>& pages() {
    static std::vector<std::unique_ptr<ViewPage>> store;
    return store;
}

HWND main_hwnd(GtkWindow* window) {
    if (!window) return nullptr;
    GtkWidget* widget = GTK_WIDGET(window);
    if (!gtk_widget_get_realized(widget)) return nullptr;
    GdkWindow* gdk = gtk_widget_get_window(widget);
    if (!gdk) return nullptr;
    return static_cast<HWND>(gdk_win32_window_get_handle(gdk));
}

HWND view_hwnd(ViewPage& page) {
    return static_cast<HWND>(page.view->native_widget());
}

void sync_geometry(ViewPage& page) {
    if (!page.attached) return;
    HWND view = view_hwnd(page);
    HWND host = main_hwnd(page.parent);
    GdkWindow* gdk = gtk_widget_get_window(page.area);
    if (!view || !host || !gdk) return;
    gint ox = 0;
    gint oy = 0;
    gdk_window_get_origin(gdk, &ox, &oy);
    POINT pt{ox, oy};
    ScreenToClient(host, &pt);
    GtkAllocation alloc;
    gtk_widget_get_allocation(page.area, &alloc);
    MoveWindow(view, pt.x, pt.y, alloc.width, alloc.height, TRUE);
}

void attach_page(ViewPage& page) {
    if (page.attached) return;
    HWND view = view_hwnd(page);
    HWND host = main_hwnd(page.parent);
    if (!view || !host) return;
    SetParent(view, host);
    page.attached = true;
}

void on_page_map(GtkWidget*, ViewPage* page) {
    attach_page(*page);
    sync_geometry(*page);
    HWND view = view_hwnd(*page);
    if (view) {
        ShowWindow(view, SW_SHOW);
        SetWindowPos(view, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    }
}

void on_page_unmap(GtkWidget*, ViewPage* page) {
    if (!page->attached) return;
    HWND view = view_hwnd(*page);
    if (view) ShowWindow(view, SW_HIDE);
}

void on_page_size_allocate(GtkWidget*, GdkRectangle*, ViewPage* page) {
    sync_geometry(*page);
}

void on_area_destroy(GtkWidget*, ViewPage* page) {
    std::vector<std::unique_ptr<ViewPage>>& store = pages();
    for (auto it = store.begin(); it != store.end(); ++it) {
        if (it->get() == page) {
            store.erase(it);
            return;
        }
    }
}

ShortcutHandler& shortcut_handler() {
    static ShortcutHandler handler;
    return handler;
}

GtkWindow* shortcut_window = nullptr;
HHOOK keyboard_hook = nullptr;
WNDPROC default_wndproc = nullptr;

struct KeyCombo {
    bool ctrl;
    bool shift;
    bool alt;
    UINT vk;
    guint keyval;
    GdkModifierType mods;
};

const KeyCombo kShortcuts[] = {
    {true, false, false, 'T', GDK_KEY_t, GDK_CONTROL_MASK},
    {true, true, false, 'P', GDK_KEY_P, static_cast<GdkModifierType>(GDK_CONTROL_MASK | GDK_SHIFT_MASK)},
    {true, false, false, 'W', GDK_KEY_w, GDK_CONTROL_MASK},
    {true, false, false, 'L', GDK_KEY_l, GDK_CONTROL_MASK},
    {true, false, false, 'F', GDK_KEY_f, GDK_CONTROL_MASK},
    {true, false, false, 'H', GDK_KEY_h, GDK_CONTROL_MASK},
    {true, false, false, 'J', GDK_KEY_j, GDK_CONTROL_MASK},
    {true, false, false, 'D', GDK_KEY_d, GDK_CONTROL_MASK},
    {true, true, false, 'O', GDK_KEY_O, static_cast<GdkModifierType>(GDK_CONTROL_MASK | GDK_SHIFT_MASK)},
    {true, false, false, VK_OEM_COMMA, GDK_KEY_comma, GDK_CONTROL_MASK},
    {true, false, false, 'R', GDK_KEY_r, GDK_CONTROL_MASK},
    {true, false, false, VK_OEM_PLUS, GDK_KEY_equal, GDK_CONTROL_MASK},
    {true, false, false, VK_OEM_MINUS, GDK_KEY_minus, GDK_CONTROL_MASK},
    {true, false, false, '0', GDK_KEY_0, GDK_CONTROL_MASK},
    {true, false, false, 'Q', GDK_KEY_q, GDK_CONTROL_MASK},
    {true, false, false, 'S', GDK_KEY_s, GDK_CONTROL_MASK},
    {false, false, false, VK_F3, GDK_KEY_F3, static_cast<GdkModifierType>(0)},
    {false, true, false, VK_F3, GDK_KEY_F3, GDK_SHIFT_MASK},
    {false, false, false, VK_F5, GDK_KEY_F5, static_cast<GdkModifierType>(0)},
    {false, false, false, VK_F11, GDK_KEY_F11, static_cast<GdkModifierType>(0)},
    {false, false, true, VK_LEFT, GDK_KEY_Left, GDK_MOD1_MASK},
    {false, false, true, VK_RIGHT, GDK_KEY_Right, GDK_MOD1_MASK},
};

bool match_shortcut(UINT vk, GdkModifierType& mods, guint& keyval) {
    bool ctrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
    bool shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
    bool alt = (GetKeyState(VK_MENU) & 0x8000) != 0;
    for (const KeyCombo& combo : kShortcuts) {
        if (combo.vk == vk && combo.ctrl == ctrl && combo.shift == shift && combo.alt == alt) {
            mods = combo.mods;
            keyval = combo.keyval;
            return true;
        }
    }
    return false;
}

LRESULT CALLBACK hook_wndproc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
    if (msg == WM_APP_SHORTCUT) {
        if (shortcut_handler()) {
            shortcut_handler()(static_cast<guint>(wparam), static_cast<GdkModifierType>(lparam));
        }
        return 0;
    }
    if (msg == WM_DESTROY && default_wndproc) {
        SetWindowLongPtrW(hwnd, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(default_wndproc));
        default_wndproc = nullptr;
    }
    return CallWindowProcW(default_wndproc, hwnd, msg, wparam, lparam);
}

void ensure_wndproc() {
    if (default_wndproc) return;
    HWND host = main_hwnd(shortcut_window);
    if (!host) return;
    default_wndproc = reinterpret_cast<WNDPROC>(
        SetWindowLongPtrW(host, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(hook_wndproc)));
}

void on_window_realize(GtkWidget*, gpointer) {
    ensure_wndproc();
}

LRESULT CALLBACK keyboard_proc(int code, WPARAM wparam, LPARAM lparam) {
    if (code == HC_ACTION && shortcut_handler()) {
        ensure_wndproc();
        bool down = (lparam & (1u << 31)) == 0;
        bool repeat = (lparam & (1u << 30)) != 0;
        HWND host = main_hwnd(shortcut_window);
        if (down && !repeat && host && GetForegroundWindow() == host) {
            GdkModifierType mods = static_cast<GdkModifierType>(0);
            guint keyval = 0;
            if (match_shortcut(static_cast<UINT>(wparam), mods, keyval)) {
                PostMessageW(host, WM_APP_SHORTCUT, static_cast<WPARAM>(keyval),
                             static_cast<LPARAM>(mods));
                return 1;
            }
        }
    }
    return CallNextHookEx(keyboard_hook, code, wparam, lparam);
}

void on_window_destroy(GtkWidget*, gpointer) {
    if (keyboard_hook) {
        UnhookWindowsHookEx(keyboard_hook);
        keyboard_hook = nullptr;
    }
    shortcut_handler() = nullptr;
    shortcut_window = nullptr;
}

}

GtkWidget* view_page_widget(mustela::View* view, GtkWindow* parent) {
    for (std::unique_ptr<ViewPage>& page : pages()) {
        if (page->view == view) return page->area;
    }
    GtkWidget* area = gtk_drawing_area_new();
    gtk_widget_set_can_focus(area, TRUE);
    auto page = std::make_unique<ViewPage>();
    page->view = view;
    page->area = area;
    page->parent = parent;
    ViewPage* raw = page.get();
    pages().push_back(std::move(page));
    g_signal_connect(area, "map", G_CALLBACK(on_page_map), raw);
    g_signal_connect(area, "unmap", G_CALLBACK(on_page_unmap), raw);
    g_signal_connect(area, "size-allocate", G_CALLBACK(on_page_size_allocate), raw);
    g_signal_connect(area, "destroy", G_CALLBACK(on_area_destroy), raw);
    return area;
}

void install_shortcuts(GtkWindow* window, ShortcutHandler handler) {
    shortcut_handler() = std::move(handler);
    shortcut_window = window;
    GtkWidget* widget = GTK_WIDGET(window);
    if (gtk_widget_get_realized(widget)) {
        ensure_wndproc();
    } else {
        g_signal_connect(window, "realize", G_CALLBACK(on_window_realize), nullptr);
    }
    if (!keyboard_hook) {
        keyboard_hook = SetWindowsHookExW(WH_KEYBOARD, keyboard_proc, nullptr, GetCurrentThreadId());
    }
    g_signal_connect(window, "destroy", G_CALLBACK(on_window_destroy), nullptr);
}

}
