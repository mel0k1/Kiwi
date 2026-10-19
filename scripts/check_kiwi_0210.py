#!/usr/bin/env python3
"""Structural sanity check for Kiwi 0.21.0 (no MSVC/CEF headers locally):

- brace/paren balance per touched source file
- the two new ready-made themes (sakura, crimson) exist across the stack
  (palette table, theme_names, store validation, settings chips, tests)
- the rail drag gesture is fully wired (grip rect, cursor, press, move,
  release, capture-changed, hit-test exclusion, affordance paint, flip +
  persist) and the settings dialog stays in sync
- stale references are gone (7-chip count, drag_rail_ checked before its
  members exist, missing sync call)
"""

import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SRC = ROOT / "src"

FILES = [
    "src/style_store.cpp",
    "src/style_store.hpp",
    "src/tests/test_style_store.cpp",
    "src/cef/win/gfx_win.cpp",
    "src/cef/win/gfx_win.hpp",
    "src/cef/win/cef_shell_win.cpp",
    "src/cef/win/settings_win.cpp",
    "src/cef/win/settings_win.hpp",
]

MUST_DEFINE = {
    "src/cef/win/gfx_win.cpp": [
        (r'\bt\.push_back\(\{"sakura", false, p, a\}\);', 1),
        (r'\bt\.push_back\(\{"crimson", true, p, a\}\);', 1),
        (r'"solarized",\n        "sakura", "crimson"', 1),
        (r"// Sakura \(pastel pink paper, rose accent\)", 1),
        (r"// Crimson \(warm near-black, red accent\)", 1),
    ],
    "src/style_store.cpp": [
        (r'theme == "solarized" \|\| theme == "sakura" \|\| theme == "crimson";', 1),
        (r"catppuccin, \"\n        \"solarized, sakura or crimson", 1),
    ],
    "src/style_store.hpp": [
        (r'"solarized", "sakura", "crimson"\)', 1),
    ],
    "src/cef/win/settings_win.cpp": [
        (r"constexpr int kThemeChipCount = 9;", 1),
        (r"constexpr UINT kSyncRailSideMsg = WM_APP \+ 0x51;", 1),
        (r"case kSyncRailSideMsg: \{", 1),
        (r"void sync_settings_rail_side\(\) \{", 1),
        (r"EnumWindows\(\[\]\(HWND hwnd, LPARAM\) -> BOOL \{", 1),
        (r"SendMessageW\(GetDlgItem\(hwnd, ID_RAILSIDE\), CB_SETCURSEL,", 1),
    ],
    "src/cef/win/settings_win.hpp": [
        (r"void sync_settings_rail_side\(\);", 1),
    ],
    "src/cef/win/cef_shell_win.cpp": [
        (r"RECT CefShell::rail_grip_rect\(\) const \{", 1),
        (r"void CefShell::flip_rail_side\(const std::string& side\) \{", 1),
        (r"RECT rail_grip_rect\(\) const;", 1),
        (r"void flip_rail_side\(const std::string& side\);", 1),
        (r"bool drag_rail_ = false;", 1),
        (r"bool rail_flip_done_ = false;", 1),
        (r"bool hover_rail_grip_ = false;", 1),
        (r"RECT grip = self->rail_grip_rect\(\);\n                if \(self->lay_\.zen && PtInRect\(&grip, pt\)\) \{", 1),
        (r"SetCursor\(LoadCursorW\(nullptr, IDC_SIZEWE\)\);\n                    return TRUE;", 1),
        (r"RECT grip = self->rail_grip_rect\(\);\n                if \(PtInRect\(&grip, cl\)\) \{\n                    self->drag_rail_ = true;", 1),
        (r"self->drag_rail_ = true;\n                    self->rail_flip_done_ = false;", 1),
        (r"if \(self->drag_rail_\) \{", 1),
        (r"self->flip_rail_side\(\"right\"\);", 1),
        (r"self->flip_rail_side\(\"left\"\);", 1),
        (r"const bool had_rail = self->drag_rail_;", 1),
        (r"if \(had_rail\) return 0;", 1),
        (r"self->drag_rail_ = false;\n            self->rail_flip_done_ = false;", 2),
        (r"RECT grip = rail_grip_rect\(\);\n    if \(PtInRect\(&grip, cl\)\) return false;", 1),
        (r"grip_hover = PtInRect\(&grip, cl\) != FALSE;", 1),
        (r"bool hot = hover_rail_grip_ \|\| drag_rail_;", 1),
        (r"hot \? pal\.omnibox_focus : pal\.outline\);", 1),
        (r"kiwi::save_shell_rail_side\(rt->config_dir, side\);", 1),
        (r"sync_settings_rail_side\(\);", 1),
        (r"grip_hover != self->hover_rail_grip_", 1),
        (r"self->hover_rail_grip_ = false;", 1),
    ],
    "src/tests/test_style_store.cpp": [
        (r'"solarized", "sakura", "crimson"\}', 1),
        (r'kiwi::load_shell_theme\(dir\) == "sakura"', 1),
        (r'!kiwi::is_valid_shell_theme\("rose"\)', 1),
    ],
}

FORBIDDEN = [
    # the chip count must track theme_names(); 7 is the stale 0.20.0 value
    ("src/cef/win/settings_win.cpp",
     [r"constexpr int kThemeChipCount = 7;"]),
    # the drag gesture must persist through the store validator, not write raw
    ("src/cef/win/cef_shell_win.cpp",
     [r"save_shell_rail_side\(rt->config_dir, rail_side_\);",
      # MSVC C2102: '&' on the RECT returned by value — bind a named local first
      r"PtInRect\(&self->rail_grip_rect\(\)",
      r"PtInRect\(&rail_grip_rect\(\)"]),
]


def balanced(text: str) -> list:
    """Strip comments/strings, then check brace/paren balance."""
    out = []
    i, n = 0, len(text)
    clean = []
    while i < n:
        c = text[i]
        if text.startswith("//", i):
            j = text.find("\n", i)
            i = n if j < 0 else j
            continue
        if text.startswith("/*", i):
            j = text.find("*/", i + 2)
            i = n if j < 0 else j + 2
            continue
        if c in "\"'":
            quote = c
            i += 1
            while i < n:
                if text[i] == "\\":
                    i += 2
                    continue
                if text[i] == quote:
                    i += 1
                    break
                i += 1
            continue
        clean.append(c)
        i += 1
    joined = "".join(clean)
    for open_ch, close_ch in (("{", "}"), ("(", ")")):
        if joined.count(open_ch) != joined.count(close_ch):
            out.append(f"unbalanced {open_ch}{close_ch}: "
                       f"{joined.count(open_ch)} vs {joined.count(close_ch)}")
    return out


def main() -> int:
    errors = []
    for rel in FILES:
        path = ROOT / rel
        text = path.read_text(encoding="utf-8")
        for issue in balanced(text):
            errors.append(f"{rel}: {issue}")
        for pattern, want in MUST_DEFINE.get(rel, []):
            found = len(re.findall(pattern, text))
            if found != want:
                errors.append(f"{rel}: pattern {pattern!r} found {found}x, want {want}")
    for rel, patterns in FORBIDDEN:
        text = (ROOT / rel).read_text(encoding="utf-8")
        for pattern in patterns:
            if re.search(pattern, text):
                errors.append(f"{rel}: forbidden pattern {pattern!r} present")

    if errors:
        for err in errors:
            print(f"FAIL: {err}", file=sys.stderr)
        return 1
    print("check_kiwi_0210: OK "
          f"({len(FILES)} files, "
          f"{sum(w for _, w in sum(MUST_DEFINE.values(), []))} symbol checks)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
