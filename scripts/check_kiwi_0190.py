#!/usr/bin/env python3
"""Structural sanity check for Kiwi 0.19.0 (no MSVC/CEF headers locally):

- brace/paren balance per touched source file
- new functions/methods defined exactly once and referenced
- stale references are gone (old transparency bool, peach surfaces)
"""

import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SRC = ROOT / "src"

FILES = [
    "style_store.cpp",
    "style_store.hpp",
    "cef/win/gfx_win.cpp",
    "cef/win/gfx_win.hpp",
    "cef/win/cef_shell_win.cpp",
    "cef/win/settings_win.cpp",
    "cef/win/search_engines_win.cpp",
    "cef/win/search_engines_win.hpp",
    "cef/win/clear_data_win.cpp",
    "cef/win/clear_data_win.hpp",
    "cef/win/aux_theme_win.hpp",
    "cef/win/bookmarks_win.cpp",
    "cef/win/history_win.cpp",
    "cef/win/site_rules_win.cpp",
]

# symbol -> min occurrences of definition regex
MUST_DEFINE = {
    "src/style_store.cpp": [
        (r"\bstd::string load_shell_backdrop\(const std::string& config_dir\) \{", 1),
        (r"\bvoid save_shell_backdrop\(const std::string& config_dir, const std::string& backdrop\) \{", 1),
        (r"\bbool is_valid_shell_backdrop\(const std::string& backdrop\) \{", 1),
    ],
    "src/cef/win/gfx_win.cpp": [
        (r"\bbool system_prefers_dark\(\) \{", 1),
        (r"\bvoid set_dark_chrome\(bool dark\) \{", 1),
        (r"\bbool dark_chrome\(\) \{", 1),
        (r"\bconst AuxTheme& aux_theme\(\) \{", 1),
        (r"\bHBRUSH aux_surface_brush\(\) \{", 1),
        (r"\bHBRUSH aux_field_brush\(\) \{", 1),
    ],
    "src/cef/win/cef_shell_win.cpp": [
        (r"constexpr DWORD kDwmsbtMica = 2;", 1),
        (r"constexpr DWORD kDwmsbtAcrylic = 3;", 1),
        (r"\bbackdrop_ = kiwi::load_shell_backdrop\(", 2),
        (r"\bgfx::set_dark_chrome\(gfx::system_prefers_dark\(\)\);", 1),
        (r"kDwmwaUseImmersiveDarkMode, &dark", 1),
    ],
    "src/cef/win/search_engines_win.cpp": [
        (r"\bvoid open_search_engines_window\(HWND owner\) \{", 1),
        (r"\bvoid save_current\(EnginesUi& ui\) \{", 1),
        (r"\bvoid set_default_selected\(EnginesUi& ui\) \{", 1),
        (r"search_engines\.push_back\(engine\)", 1),
        (r"mustela::save_settings\(rt->settings_path, rt->settings\);", 2),
    ],
    "src/cef/win/clear_data_win.cpp": [
        (r"\bvoid open_clear_data_window\(HWND owner\) \{", 1),
        (r"ClearHttpCache\(new DoneCallback\(hwnd\)\)", 1),
        (r"ClearHttpAuthCredentials\(new DoneCallback\(hwnd\)\);", 1),
        (r"ClearCertificateExceptions\(new DoneCallback\(hwnd\)\);", 1),
        (r"DeleteCookies\(CefString\(\), CefString\(\), nullptr\);", 1),
        (r"rt->history->clear\(\);", 1),
    ],
    "src/cef/win/settings_win.cpp": [
        (r"open_search_engines_window\(st->hwnd\);", 1),
        (r"open_clear_data_window\(st->hwnd\);", 1),
        (r"ID_BACKDROP,", 1),
        (r"save_shell_backdrop\(rt->config_dir, backdrop\);", 1),
    ],
}

FORBIDDEN = [
    ("src/cef/win/cef_shell_win.cpp", [r"\btransparency_", r"constexpr DWORD kDwmsbtMica = 1;"]),
    ("src/cef/win/bookmarks_win.cpp", [r"255, 245, 240"]),
    ("src/cef/win/history_win.cpp", [r"255, 245, 240"]),
    ("src/cef/win/site_rules_win.cpp", [r"255, 245, 240"]),
    ("src/cef/win/settings_win.cpp", [r"255, 245, 240", r"ID_TRANSPARENCY"]),
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
