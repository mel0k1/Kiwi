#!/usr/bin/env python3
"""Structural checks for Kiwi 0.22.0 (workspaces + Shields).

What the local toolchain cannot verify (MSVC/CEF compile happens in CI) this
script guards cheaply: brace balance over the touched sources, the presence
of every new seam in the right file, the CMake wiring (sources, tests, the
Shields blocklist copy) and the MSVC trap patterns that already burned us in
0.21.0 (address-of-temporary RECT).
"""

import re
import sys

ROOT = "/home/z/my-project/Kiwi"

BALANCE_FILES = [
    "src/cef/win/cef_shell_win.cpp",
    "src/cef/win/cef_shell_win.hpp",
    "src/cef/win/settings_win.cpp",
    "src/cef/win/gfx_win.cpp",
    "src/cef/win/gfx_win.hpp",
    "src/cef/win/workspaces_win.cpp",
    "src/cef/win/workspaces_win.hpp",
    "src/cef/win/shields_win.cpp",
    "src/cef/win/shields_win.hpp",
    "src/cef/cef_client.cpp",
    "src/cef/cef_client.hpp",
    "src/cef/cef_bridge.hpp",
    "src/cef/cef_runtime.cpp",
    "src/cef/cef_runtime.hpp",
    "src/shields_rules.cpp",
    "src/shields_store.cpp",
    "src/workspaces_store.cpp",
]

REQUIRED = {
    "src/workspaces_store.hpp": [
        "kMaxWorkspaces", "kWorkspaceGlyphs", "WorkspaceDef",
        "load_workspaces", "save_workspaces", "next_workspace_id",
        "WorkspaceSessionState", "save_workspace_session",
        "load_workspace_session",
    ],
    "src/workspaces_store.cpp": [
        "\"home\", \"briefcase\", \"code\", \"gamepad\", \"cart\", \"news\"",
        "workspace-session.toml", "workspaces.toml",
    ],
    "src/shields_rules.hpp": [
        "class Rules", "load_file", "hit", "valid_domain", "unordered_set",
    ],
    "src/shields_rules.cpp": [
        "||", "0.0.0.0", "127.0.0.1", "strip_rule_syntax",
    ],
    "src/shields_store.hpp": [
        "ShieldsConfig", "load_shields", "save_shields",
        "shields_site_mode", "shields_set_site_mode",
    ],
    "src/shields_store.cpp": ["shields.toml", "[sites]"],
    "src/cef/win/cef_shell_win.cpp": [
        "WM_APP_SYNC_WS", "kShldToggle", "kShldGlobal", "kShldCosmetic",
        "kShldExceptions", "TABCTX_WS_BASE", "Move to workspace",
        "ws_pills_", "ws_chips_", "ws_add_pill_", "active_ws_",
        "ws_active_tab_", "switch_workspace", "move_tab_to_workspace",
        "reload_workspaces_state", "load_workspaces_state",
        "open_shields_menu", "shields_session_total", "on_shields",
        "sync_workspaces", "draw_ws_chips", "No tabs in this workspace",
        "Blocked on this page", "Blocked in this session",
        "Shield exceptions...",
        "save_workspace_session", "load_workspace_session",
        "shields_site_mode", "open_workspaces_window", "open_shields_window",
        "workspace = workspace > 0 ? workspace : active_ws_",
    ],
    "src/cef/win/cef_shell_win.hpp": ["sync_workspaces"],
    "src/cef/cef_bridge.hpp": ["HK_WS_1", "HK_WS_8", "on_shields"],
    "src/cef/cef_client.hpp": [
        "OnLoadStart", "shields_page_host", "blocked_page_", "page_host_",
    ],
    "src/cef/cef_client.cpp": [
        "kCosmeticSelectors", "cosmetic_script", "kiwi-shields",
        "shields_page_host", "rt->shields_cfg.enabled",
        "rt->shields_rules.hit", "on_shields", "HK_WS_1",
        "ExecuteJavaScript", "taboola", "div-gpt-ad", "adsbygoogle",
    ],
    "src/cef/cef_runtime.hpp": ["shields::Rules", "ShieldsConfig shields_cfg"],
    "src/cef/cef_runtime.cpp": [
        "load_shields", "shields-blocklist.txt", "GetModuleFileNameW",
    ],
    "src/cef/win/gfx_win.hpp": [
        "Briefcase", "Gamepad", "Cart", "News", "Music", "Mail",
        "Heart", "Flame", "glyph_from_name",
    ],
    "src/cef/win/gfx_win.cpp": [
        "Glyph::Briefcase", "Glyph::Code", "Glyph::Gamepad", "Glyph::Cart",
        "Glyph::News", "Glyph::Music", "Glyph::Mail", "Glyph::Heart",
        "Glyph::Flame", "glyph_from_name",
    ],
    "src/cef/win/settings_win.cpp": [
        "ID_WORKSPACES", "ID_SHIELDS", "ID_SHIELDS_COSMETIC",
        "ID_SHIELD_EXC", "ID_SHIELDS_SIZE", "Block ads and trackers",
        "Hide ad placeholders", "Shield exceptions...", "Workspaces...",
        "Blocklist: ", "shields::save_shields", "open_workspaces_window",
        "open_shields_window",
    ],
    "src/cef/win/workspaces_win.cpp": [
        "KiwiWorkspacesWindow", "open_workspaces_window", "sync_workspaces",
        "next_workspace_id", "kMaxWorkspaces", "glyph_from_name",
    ],
    "src/cef/win/shields_win.cpp": [
        "KiwiShieldsWindow", "open_shields_window", "shields_set_site_mode",
        "shields::normalize_host", "valid_domain", "Glyph::Shield",
    ],
}

CMAKE_REQUIRED = {
    "CMakeLists.txt": [
        "VERSION 0.22.0", "kiwi_shields_tests", "kiwi_workspaces_tests",
        "src/shields_rules.cpp src/shields_store.cpp",
    ],
    "cmake/kiwi-cef.cmake": [
        "src/cef/win/workspaces_win.cpp", "src/cef/win/shields_win.cpp",
        "src/shields_rules.cpp", "src/shields_store.cpp",
        "src/workspaces_store.cpp", "kiwi_shields_tests",
        "kiwi_workspaces_tests", "shields-blocklist.txt",
        "assets/shields/blocklist.txt",
    ],
}

# MSVC trap from 0.21.0: address of a temporary returned by a function.
FORBIDDEN_SHELL = [
    (r"PtInRect\(&[a-z_][a-z0-9_]*\(", "address-of-temporary RECT in PtInRect"),
    (r"GetWindowRect\(&[a-z_][a-z0-9_]*\(", "address-of-temporary RECT"),
    (r"tab_width_for\(\s*static_cast<int>\(self->tabs_\.size\(\)\)",
     "stale full-tab-width call (must use the workspace count)"),
    (r"tab_chip_rect\(tab,", "chip rect indexed by a global index (must be slot)"),
]


def read(path):
    with open(f"{ROOT}/{path}", encoding="utf-8") as handle:
        return handle.read()


def main():
    failures = []

    for path in BALANCE_FILES:
        text = read(path)
        if text.count("{") != text.count("}"):
            failures.append(
                f"{path}: brace imbalance {{={text.count('{')} }}={text.count('}')}")

    for path, symbols in REQUIRED.items():
        text = read(path)
        for symbol in symbols:
            if symbol not in text:
                failures.append(f"{path}: missing '{symbol}'")

    for path, symbols in CMAKE_REQUIRED.items():
        text = read(path)
        for symbol in symbols:
            if symbol not in text:
                failures.append(f"{path}: missing '{symbol}'")

    shell = read("src/cef/win/cef_shell_win.cpp")
    for pattern, what in FORBIDDEN_SHELL:
        for match in re.finditer(pattern, shell):
            line = shell.count("\n", 0, match.start()) + 1
            failures.append(f"src/cef/win/cef_shell_win.cpp:{line}: {what}")

    if failures:
        for failure in failures:
            print(f"FAIL: {failure}")
        print(f"check_kiwi_0220: {len(failures)} failures")
        return 1
    print("check_kiwi_0220: PASS")
    return 0


if __name__ == "__main__":
    sys.exit(main())
