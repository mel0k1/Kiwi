#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

namespace kiwi {

// Opens the search engines editor (modeless, owner-disabled while open).
// Edits mustela-core's shared settings store: add/edit/remove engines
// (keyword, display name, %s URL template) and pick the default one. The
// omnibox picks the changes up immediately through resolve_entry — !keyword
// shortcuts and the default engine included.
void open_search_engines_window(HWND owner);

}
