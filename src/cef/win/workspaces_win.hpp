#pragma once

#include <windows.h>

namespace kiwi {

// Opera-style workspace manager: list of workspaces (glyph + name), inline
// rename, glyph combo, add/remove. Writes <config>/workspaces.toml through
// the shared store and pokes the shell windows via sync_workspaces(). The
// last remaining workspace cannot be removed (its tabs would have nowhere
// to go).
void open_workspaces_window(HWND owner);

}

