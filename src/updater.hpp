#pragma once

#include <glib.h>

#include <atomic>
#include <string>

struct Browser;

// Check-for-updates against GitHub Releases (manual, on demand only —
// Kiwi never phones home in the background). The network work runs on a
// worker thread; every UI touch happens through g_idle_add on the main loop.

namespace kiwi {

struct UpdateInfo {
    std::atomic<bool> checking{false};
    std::atomic<bool> busy{false};
    std::atomic<bool> available{false};
    std::atomic<bool> up_to_date{false};
    std::atomic<int> percent{0};

    std::string current_version = KIWI_VERSION;
    std::string latest_version;
    std::string notes;
    std::string release_url;
    std::string asset_url;
    std::string asset_name;
    std::string error;
    std::string downloaded_path;

    guint poll_id = 0;
};

UpdateInfo& updates();

void updates_check(Browser* b);
void updates_download(Browser* b);
void updates_open_folder(Browser* b);
void updates_shutdown();

// True while the settings-page widgets are alive (prefs.cpp owns them).
bool updates_ui_bound();

// Implemented in prefs.cpp (owns the settings-page widgets): called on the
// main loop after every state change; a null b just repaints the progress.
void updates_refresh_ui(Browser* b);

}

#ifndef KIWI_VERSION
#define KIWI_VERSION "0.0.0"
#endif
