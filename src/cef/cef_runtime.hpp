#pragma once

#include <memory>
#include <string>
#include <vector>

#include "mustela/mustela.hpp"
#include "blocklist.hpp"
#include "site_rules.hpp"
#include "../shields_rules.hpp"
#include "../shields_store.hpp"

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
    std::unique_ptr<mustela::HistoryStore> history;
    // Per-site overrides (config_dir/sites.toml), shared with the GTK build.
    std::unique_ptr<mustela::SiteRules> site_rules;
    // Path of the store above, handed to CEF subprocesses via a command-line
    // switch so the render process can resolve per-site fingerprint rules.
    std::string site_rules_path;

    // Kiwi Shields: the fast ad/tracker host matcher (loaded once at startup
    // from the blocklist file shipped next to kiwi.exe; when that file is
    // missing the mustela tracker blocklist above stays the only network
    // filter) and the user-facing shields.toml configuration. Browser
    // process only — the render process never sees these.
    shields::Rules shields_rules;
    shields::ShieldsConfig shields_cfg;

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
