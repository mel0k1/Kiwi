#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

namespace kiwi {

// Opens the per-site rules editor (modeless, owner-disabled while open).
// Edits the same <config>/sites.toml store the GTK build and the CEF
// request/render layers read. Rules: JavaScript, Cookies, Fingerprint —
// default/allow/deny per host. Zoom/user-agent/CSS fields of an entry are
// preserved on edit (not editable in this dialog yet).
void open_site_rules_dialog(HWND owner);

}
