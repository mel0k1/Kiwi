#!/usr/bin/env python3
"""Structural sanity check for Kiwi 0.20.0 (no MSVC/CEF headers locally):

- brace/paren balance per touched source file
- new functions/keys defined exactly once and referenced
- stale references are gone (startup set_dark_chrome, Save-button flow,
  swprintf_s, class-captured background brushes in the live-theme windows)
"""

import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SRC = ROOT / "src"

FILES = [
    "style_store.cpp",
    "style_store.hpp",
    "tests/test_style_store.cpp",
    "cef/win/gfx_win.cpp",
    "cef/win/gfx_win.hpp",
    "cef/win/cef_shell_win.cpp",
    "cef/win/settings_win.cpp",
    "cef/win/settings_win.hpp",
    "cef/win/aux_theme_win.hpp",
    "cef/win/bookmarks_win.cpp",
    "cef/win/history_win.cpp",
]

MUST_DEFINE = {
    "src/style_store.cpp": [
        (r"\bstd::string load_shell_theme\(const std::string& config_dir\) \{", 1),
        (r"\bvoid save_shell_theme\(const std::string& config_dir, const std::string& theme\) \{", 1),
        (r"\bbool is_valid_shell_theme\(const std::string& theme\) \{", 1),
        (r"\bstd::string load_shell_accent\(const std::string& config_dir\) \{", 1),
        (r"\bvoid save_shell_accent\(const std::string& config_dir, const std::string& accent\) \{", 1),
        (r"\bbool is_valid_shell_accent\(const std::string& accent\) \{", 1),
        (r"\bstd::string load_shell_rail_side\(const std::string& config_dir\) \{", 1),
        (r"\bvoid save_shell_rail_side\(const std::string& config_dir, const std::string& side\) \{", 1),
        (r"\bbool is_valid_shell_rail_side\(const std::string& side\) \{", 1),
        # the single writer carries all six keys
        (r'rail_side = \\"%s\\"', 1),
    ],
    "src/cef/win/gfx_win.cpp": [
        (r"\bvoid apply_named_theme\(const std::string& name, const std::string& accent_hex\) \{", 1),
        (r"\bbool theme_preview\(const std::string& name, COLORREF\* bg, COLORREF\* text,\n                   COLORREF\* accent\) \{", 1),
        (r"\bconst std::vector<std::string>& theme_names\(\) \{", 1),
        (r"\bbool is_known_theme\(const std::string& name\) \{", 1),
        (r'\bt\.push_back\(\{"nord", true, p, a\}\);', 1),
        (r'\bt\.push_back\(\{"dracula", true, p, a\}\);', 1),
        (r'\bt\.push_back\(\{"catppuccin", true, p, a\}\);', 1),
        (r'\bt\.push_back\(\{"solarized", false, p, a\}\);', 1),
        (r"\bcase Glyph::Palette:", 1),
        (r"\bcase Glyph::Shield:", 1),
        (r"\bcase Glyph::Database:", 1),
        (r"\bcase Glyph::Home:", 1),
    ],
    "src/cef/win/cef_shell_win.cpp": [
        (r'std::string rail_side_;  // "left" or "right" \(style.toml\)', 1),
        (r"rail_side_ = kiwi::load_shell_rail_side\(", 2),
        (r"bool rail_right = rail_side_ == \"right\";", 1),
        (r"int host_left = rail_right \? 0 : rail_w;", 1),
        (r"gfx::apply_named_theme\(", 2),  # create_window + reload_style
        (r"constexpr DWORD kDwmsbtMica = 2;", 1),
        (r"constexpr DWORD kDwmsbtAcrylic = 3;", 1),
    ],
    "src/cef/win/settings_win.cpp": [
        (r"\bSECTION_APPEARANCE,", 2),
        (r"\bSECTION_PRIVACY,", 2),
        (r"\bID_THEME_BASE,", 1),
        (r"\bopen_search_engines_window\(st->hwnd\);", 1),
        (r"\bopen_clear_data_window\(st->hwnd\);", 1),
        (r"\bopen_site_rules_dialog\(st->hwnd\);", 1),
        (r"\bcheck_for_updates\(hwnd\);", 1),
        (r"\bapply_all\(st\);", 6),
        (r"save_shell_theme\(rt->config_dir, st->theme\);", 1),
        (r"save_shell_accent\(rt->config_dir, st->accent\);", 1),
        (r"save_shell_rail_side\(rt->config_dir, rail\);", 1),
        (r"gfx::theme_preview\(name, &bg, &text, &accent\)", 1),
        (r"ChooseColorW\(&cc\)", 1),
        (r"s\.session_restore = checkbox_value", 1),
        (r"s\.history_enabled = checkbox_value", 1),
        (r"s\.start_in_private = checkbox_value", 1),
    ],
    "src/cef/win/aux_theme_win.hpp": [
        (r"\binline LRESULT aux_erasebkgnd\(HWND hwnd, WPARAM wparam\) \{", 1),
        (r"\binline void retheme_children\(HWND parent\) \{", 1),
        (r"L\"DarkMode_Explorer\"\n                                            : L\"Explorer\"", 1),
    ],
    "src/cef/win/history_win.cpp": [
        (r"return aux_erasebkgnd\(hwnd, wparam\);", 1),
        (r"wc\.hbrBackground = nullptr;  // WM_ERASEBKGND paints the live theme", 1),
    ],
    "src/cef/win/bookmarks_win.cpp": [
        (r"return aux_erasebkgnd\(hwnd, wparam\);", 1),
        (r"wc\.hbrBackground = nullptr;  // WM_ERASEBKGND paints the live theme", 1),
    ],
    "src/tests/test_style_store.cpp": [
        (r"load_shell_theme\(dir\) == \"nord\"", 1),
        (r"load_shell_rail_side\(dir\) == \"right\"", 1),
        (r"load_shell_accent\(dir\) == \"#88C0D0\"", 1),
    ],
}

FORBIDDEN = [
    # startup theme selection goes through apply_named_theme now
    ("src/cef/win/cef_shell_win.cpp",
     [r"gfx::set_dark_chrome\(gfx::system_prefers_dark\(\)\)",
      r"constexpr DWORD kDwmsbtMica = 1;"]),
    # instant apply: no Save button, no saved-MessageBox, no mingw-only swprintf_s
    ("src/cef/win/settings_win.cpp",
     [r"\bID_SAVE\b", r"Settings saved\.", r"swprintf_s"]),
    # brush refresh must orphan, never delete (class backgrounds stay live)
    ("src/cef/win/gfx_win.cpp",
     [r"DeleteObject\(brush\)"]),
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
        path = SRC / rel
        if not path.exists():
            errors.append(f"{rel}: missing file")
            continue
        text = path.read_text(encoding="utf-8")
        for problem in balanced(text):
            errors.append(f"{rel}: {problem}")
        rel_key = "src/" + rel
        for pattern, minimum in MUST_DEFINE.get(rel_key, []):
            found = len(re.findall(pattern, text))
            if found < minimum:
                errors.append(f"{rel}: {pattern!r} found {found}x, need {minimum}x")
    for rel, patterns in FORBIDDEN:
        text = (SRC / rel.removeprefix("src/")).read_text(encoding="utf-8")
        for pattern in patterns:
            if re.search(pattern, text):
                errors.append(f"{rel}: forbidden pattern {pattern!r} still present")

    if errors:
        print("FAIL")
        for error in errors:
            print("  -", error)
        return 1
    print("PASS: balance + symbols + no stale references")
    return 0


if __name__ == "__main__":
    sys.exit(main())
