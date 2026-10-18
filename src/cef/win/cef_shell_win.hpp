#pragma once

namespace kiwi {

// Runs the Win32 shell: window, tabs, omnibox, first tab. Blocks in the CEF
// message loop until the last tab closes. Returns the process exit code.
// With |self_test| the shell schedules its own check: after a fixed wait it
// verifies a browser was created, then destroys the window and exits.
int cef_shell_main(bool self_test);

// Result of the last self-test run (0 = pass).
int cef_self_test_exit_code();

}
