#pragma once

#include <string>
#include <utility>
#include <vector>

// Kiwi Shields — user-facing configuration, stored Kiwi-side in
// <config>/shields.toml (same policy as style.toml: independent from the
// shared mustela-core settings so the GTK build can adopt it later).
// Per-site exceptions follow the Brave Shields model: every site is
// "up" (blocking) unless the user turned shields "down" for it; the map only
// stores explicit overrides.

namespace kiwi {
namespace shields {

struct ShieldsConfig {
    // Master switch for the network blocking and cosmetic filtering.
    bool enabled = true;
    // Hide ad placeholders on pages (CSS element hiding for common frames).
    bool cosmetic = true;
    // Per-site overrides: host -> "up" (kept for explicitness, rarely used)
    // or "down" (shields lowered for this site).
    std::vector<std::pair<std::string, std::string>> sites;
};

ShieldsConfig load_shields(const std::string& config_dir);
void save_shields(const std::string& config_dir, const ShieldsConfig& config);

// Effective mode for |host|: "down" when an explicit exception says so,
// "up" otherwise (unknown hosts block by default while shields are enabled).
std::string shields_site_mode(const ShieldsConfig& config, const std::string& host);

// Sets or clears the per-site override. "up" removes the exception entry,
// "down" adds (or keeps) one. Any other mode is ignored.
void shields_set_site_mode(ShieldsConfig& config, const std::string& host,
                           const std::string& mode);

}

}

