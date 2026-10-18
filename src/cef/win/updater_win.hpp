#pragma once

#include <windows.h>

namespace kiwi {

// Asks GitHub for the latest release and compares it to the running version.
// Shows a message box with the verdict and offers the releases page when a
// newer version exists. Strictly manual: nothing runs in the background.
void check_for_updates(HWND owner);

}  // namespace kiwi
