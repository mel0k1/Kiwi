#pragma once

#include <functional>
#include <string>
#include <windows.h>

namespace kiwi {

// Opens the browsing history window (owned by the shell window; |open_url|
// fires on double click / Open). Only one instance exists; a second call
// brings it to front.
void open_history_window(HWND owner,
                         std::function<void(const std::string&)> open_url);

}  // namespace kiwi
