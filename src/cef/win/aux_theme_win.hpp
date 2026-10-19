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

// Dark-friendly visual styles for a child control (no-op in light mode).
inline void apply_control_theme(HWND ctrl) {
    if (ctrl && gfx::dark_chrome()) {
        SetWindowTheme(ctrl, L"DarkMode_Explorer", nullptr);
    }
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

}  // namespace kiwi
