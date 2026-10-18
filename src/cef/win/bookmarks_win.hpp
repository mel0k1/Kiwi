#pragma once

#include <functional>
#include <string>
#include <windows.h>

namespace kiwi {

// Opens the bookmarks manager window (owned by the shell window, so it closes
// together with it). |open_url| is invoked when the user asks to open a
// bookmark (double click or the Open button). Only one manager window exists;
// a second call brings the existing one to front.
void open_bookmarks_manager(HWND owner,
                            std::function<void(const std::string&)> open_url);

}  // namespace kiwi
