#pragma once

namespace kiwi {

// Runs the Win32 shell: window, tabs, omnibox, first tab. Blocks in the CEF
// message loop until the last tab closes. Returns the process exit code.
int cef_shell_main();

}
