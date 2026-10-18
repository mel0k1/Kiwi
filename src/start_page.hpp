#pragma once

// Kiwi — internal start page (new tab page).
//
// Rendered as a self-contained HTML file into the profile root and loaded via
// a file:// URI, so it works identically on the GTK and WinCairo ports without
// custom scheme support in the engine. The page carries: a search card wired
// to the configured default engine, quick-launch tiles built from bookmarks,
// a clock and a private-mode banner. Regenerated on every open so bookmark
// tiles and the engine stay fresh.

#include <string>
#include <vector>

#include "mustela/mustela.hpp"

namespace kiwi {

// Pseudo-address of the start page, usable in the omnibox and as homepage.
constexpr const char* const kStartPageUri = "kiwi://start";

bool is_start_page_entry(const std::string& url);

// Renders and writes start.html under profile_root, returns the file URI to
// load. private_mode swaps the greeting for a private-browsing banner.
std::string write_start_page(const std::string& profile_root,
                             const mustela::Settings& settings,
                             const std::vector<mustela::BookmarkEntry>& bookmarks,
                             bool private_mode);

}
