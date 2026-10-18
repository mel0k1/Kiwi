#pragma once

#include <string>

// Kiwi — shell style preference ("classic" or "zen") and the Mica
// transparency toggle, stored Kiwi-side in <config>/style.toml so they
// survive updates and stay independent from the shared mustela-core
// settings file (the GTK build can adopt the same store later).

namespace kiwi {

// Returns the stored style, "classic" by default (unknown values fall back).
std::string load_shell_style(const std::string& config_dir);

// Persists the style; "classic" and "zen" are the accepted values.
void save_shell_style(const std::string& config_dir, const std::string& style);

// True when the value is one of the accepted style names.
bool is_valid_shell_style(const std::string& style);

// Mica transparency toggle. Default follows the style: on for zen, off for
// classic (the renderer falls back to opaque chrome pre-Windows 11 22H2).
bool load_shell_transparency(const std::string& config_dir);
void save_shell_transparency(const std::string& config_dir, bool enabled);

}
