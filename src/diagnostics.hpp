#pragma once

#include <string>

// Kiwi — crash and diagnostics logging.
//
// Windows GUI builds have no console: an unhandled exception or a GTK
// critical used to vanish silently. Both are now captured under the Kiwi
// config directory (%LocalAppData%/kiwi or ~/.config/kiwi):
//   crash.log  — one line per fatal exception (code, address, top frames)
//   crash.dmp  — minidump for the last fatal exception (Windows only)
//   kiwi.log   — GTK/GLib error-level messages

namespace kiwi {

// Returns the diagnostics directory (created on demand).
std::string diagnostics_dir();

void install_crash_handler();
void install_gtk_log_file();

}
