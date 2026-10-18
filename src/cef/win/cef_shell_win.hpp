#pragma once

#include <functional>
#include <string>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

namespace kiwi {

// Runs the Win32 shell: window, tabs, omnibox, first tab. Blocks in the CEF
// message loop until the last tab closes. Returns the process exit code.
// With |self_test| the shell schedules its own check: after a fixed wait it
// verifies a browser was created, then destroys the window and exits.
int cef_shell_main(bool self_test);

// Result of the last self-test run (0 = pass).
int cef_self_test_exit_code();

// Main window of the running shell (nullptr while the shell is not up).
HWND shell_main_hwnd();

// Opens |url| in a new tab of the running shell; no-op when it is down.
void shell_open_url(const std::string& url);

}
