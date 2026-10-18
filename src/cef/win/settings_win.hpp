#pragma once

#include <functional>
#include <windows.h>

namespace kiwi {

// Opens the settings window (owned by the shell window). |on_style_changed|
// fires after a successful save when the shell style was switched, so the
// caller can relayout immediately.
void open_settings_window(HWND owner, std::function<void()> on_style_changed);

}  // namespace kiwi
