#pragma once

// Shared theming for the classic Win32 aux windows (settings, history,
// bookmarks, per-site rules, search engines, clear-data). Everything reads
// the same light/dark pair as the painted chrome (gfx::aux_theme), so the
// dialogs match the shell instead of the old peach surfaces.

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <commctrl.h>
#include <uxtheme.h>

#include "../cef_runtime.hpp"
#include "gfx_win.hpp"

namespace kiwi {

// Visual styles for a child control: DarkMode_Explorer while the chrome is
// dark, the plain Explorer theme when it is light. Explicit reset for the
// light side is what makes a LIVE theme switch re-skin controls that were
// once dark-themed.
inline void apply_control_theme(HWND ctrl) {
    if (!ctrl) return;
    SetWindowTheme(ctrl, gfx::dark_chrome() ? L"DarkMode_Explorer"
                                            : L"Explorer", nullptr);
}

// ListView colors without custom draw: the documented SetBkColor family.
inline void apply_list_theme(HWND list) {
    if (!list) return;
    const gfx::AuxTheme& t = gfx::aux_theme();
    ListView_SetBkColor(list, t.field_bg);
    ListView_SetTextBkColor(list, t.field_bg);
    ListView_SetTextColor(list, t.ink);
    apply_control_theme(list);
}

// WM_CTLCOLORSTATIC for labels painted on the dialog surface.
inline LRESULT aux_static_ctlcolor(HDC dc) {
    const gfx::AuxTheme& t = gfx::aux_theme();
    SetBkColor(dc, t.surface);
    SetTextColor(dc, t.ink);
    return reinterpret_cast<LRESULT>(gfx::aux_surface_brush());
}

// WM_CTLCOLORSTATIC for the muted hint labels.
inline LRESULT aux_muted_ctlcolor(HDC dc) {
    const gfx::AuxTheme& t = gfx::aux_theme();
    SetBkColor(dc, t.surface);
    SetTextColor(dc, t.muted);
    return reinterpret_cast<LRESULT>(gfx::aux_surface_brush());
}

// WM_CTLCOLOREDIT / WM_CTLCOLORLISTBOX for input fields and lists.
inline LRESULT aux_field_ctlcolor(HDC dc) {
    const gfx::AuxTheme& t = gfx::aux_theme();
    SetBkColor(dc, t.field_bg);
    SetTextColor(dc, t.ink);
    return reinterpret_cast<LRESULT>(gfx::aux_field_brush());
}

// WM_ERASEBKGND for the aux windows: fill with the CURRENT surface brush.
// The class brush was captured at registration time and goes stale after a
// live theme switch; erasing through the live brush keeps the window in the
// active theme. Register the window class with hbrBackground = nullptr.
inline LRESULT aux_erasebkgnd(HWND hwnd, WPARAM wparam) {
    HDC dc = reinterpret_cast<HDC>(wparam);
    if (!dc || !hwnd) return 1;
    RECT rect;
    if (!GetClientRect(hwnd, &rect)) return 1;
    FillRect(dc, &rect, gfx::aux_surface_brush());
    return 1;
}

// Re-applies the control visual styles and repaints after a live theme
// switch (apply_named_theme already rebuilt the brushes).
inline void retheme_children(HWND parent) {
    if (!parent) return;
    EnumChildWindows(parent, [](HWND ctrl, LPARAM) -> BOOL {
        apply_control_theme(ctrl);
        InvalidateRect(ctrl, nullptr, TRUE);
        return TRUE;
    }, 0);
    InvalidateRect(parent, nullptr, TRUE);
}

}  // namespace kiwi
