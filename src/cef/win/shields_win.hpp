#pragma once

#include <windows.h>

namespace kiwi {

// Kiwi Shields exceptions editor: the list of sites with lowered shields
// (shields.toml [sites]), add/remove. Writes through the shared
// shields_store; the runtime configuration is re-read in place so the
// network filter picks the change up without a restart.
void open_shields_window(HWND owner);

}

