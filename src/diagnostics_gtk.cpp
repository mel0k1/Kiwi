#include "diagnostics.hpp"

#include <glib.h>

#include <string>

namespace kiwi {

namespace {

void gtk_log_to_file(const gchar* log_domain, GLogLevelFlags level, const gchar* message,
                     gpointer) {
    (void)level;
    std::string line = std::string(log_domain ? log_domain : "gtk") + ": " + message;
    FILE* file = std::fopen((diagnostics_dir() + "/kiwi.log").c_str(), "ab");
    if (!file) return;
    std::fputs(line.c_str(), file);
    std::fputc('\n', file);
    fclose(file);
}

}  // namespace

void install_gtk_log_file() {
    g_log_set_handler(nullptr, static_cast<GLogLevelFlags>(G_LOG_LEVEL_ERROR
            | G_LOG_LEVEL_CRITICAL | G_LOG_FLAG_FATAL | G_LOG_FLAG_RECURSION),
        gtk_log_to_file, nullptr);
    g_log_set_handler("Gtk", static_cast<GLogLevelFlags>(G_LOG_LEVEL_ERROR
            | G_LOG_LEVEL_CRITICAL | G_LOG_FLAG_FATAL | G_LOG_FLAG_RECURSION),
        gtk_log_to_file, nullptr);
    g_log_set_handler("GLib-GObject", static_cast<GLogLevelFlags>(G_LOG_LEVEL_ERROR
            | G_LOG_LEVEL_CRITICAL | G_LOG_FLAG_FATAL | G_LOG_FLAG_RECURSION),
        gtk_log_to_file, nullptr);
}

}
