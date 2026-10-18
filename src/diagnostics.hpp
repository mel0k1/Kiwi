#pragma once

#include <string>

// Kiwi — crash and diagnostics logging.
//
// Windows GUI builds have no console: an unhandled exception or a GTK
// critical used to vanish silently. Both are now captured under the Kiwi
// config directory (%LocalAppData%/kiwi or ~/.config/kiwi):
//   crash.log    — one line per fatal exception (code, address, top frames)
//   crash.dmp    — minidump for the last fatal exception (Windows only)
//   kiwi.log     — GTK/GLib error-level messages
//   startup.log  — one line per startup stage; if the process dies between
//                  stages the last line names the one that never completed

namespace kiwi {

// Returns the diagnostics directory (created on demand).
std::string diagnostics_dir();

// Appends a timestamped line to startup.log. Safe to call at any point of
// process life (including before runtime_init).
void trace_stage(const std::string& message);

// Logs a fatal startup error to startup.log and shows it to the user (a
// message box on Windows instead of a silent process exit; stderr on POSIX).
void fatal_dialog(const std::string& title, const std::string& detail);

void install_crash_handler();
void install_gtk_log_file();

}
