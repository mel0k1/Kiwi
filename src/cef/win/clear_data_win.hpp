#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

namespace kiwi {

// Opens the "Clear browsing data" window (modeless, owner-disabled while
// open). Clears live through the CEF 152 request-context APIs: browsing
// history (mustela-core store), cookies (global cookie manager), the HTTP
// cache, HTTP-auth credentials and certificate exceptions. Private tabs keep
// their separate ephemeral context by design.
void open_clear_data_window(HWND owner);

}
