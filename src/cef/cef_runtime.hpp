#pragma once

#include <memory>
#include <string>
#include <vector>

#include "mustela/mustela.hpp"
#include "blocklist.hpp"

namespace kiwi {

// Browser-process runtime state shared by the CEF handlers: settings from
// mustela-core (settings.toml), the privacy policy derived from them, the
// tracker blocklist and all data paths. Built once before CefInitialize and
// treated as read-only afterwards; handler callbacks on any thread only read.
struct Runtime {
    std::string config_dir;
    std::string settings_path;
    std::string profile_root;
    std::string cache_root;
    std::string downloads_dir;

    mustela::Settings settings;
    mustela::PrivacyPolicy policy;
    mustela::Blocklist blocklist;
    std::unique_ptr<mustela::BookmarkStore> bookmarks;

    // Renders the start page into the profile root and returns its file URI.
    std::string start_page_uri(bool private_mode);
};

// Locates the config directory (%LocalAppData%/kiwi or $XDG_CONFIG_HOME/kiwi),
// loads settings.toml, builds the policy/blocklist/bookmark store. Safe to
// call in the browser process only; the CEF subprocesses must not call it.
void runtime_init();

// Returns the runtime singleton; null before runtime_init().
Runtime* runtime();

}
