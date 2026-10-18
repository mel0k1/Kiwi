#pragma once

#include <functional>
#include <string>
#include <vector>

namespace kiwi {
namespace favicons {

// Installs the UI-thread notification fired whenever new icon data is ready.
void set_notifier(std::function<void()> notify);

// Reports the icon URLs a page advertises (CefDisplayHandler::OnFaviconURLChange).
// Starts a download on the UI thread; results arrive via the notifier.
void update_urls(const std::string& host, const std::vector<std::string>& urls);

// Returns an opaque image handle for |host| (drawn with gfx::draw_image_fit)
// or nullptr. Loads from the profile disk cache on first use; call from the
// UI thread only. The pointer stays valid for the process lifetime.
void* bitmap(const std::string& host);

// Profile directory that holds the favicons/<host>.png disk cache consumed
// also by the start page tiles.
void set_profile_root(const std::string& profile_root);

}
}
