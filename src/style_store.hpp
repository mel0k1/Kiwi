#pragma once

#include <string>

// Kiwi — shell style preference ("classic" or "zen").
//
// Stored Kiwi-side in <config>/style.toml so it survives updates and stays
// independent from the shared mustela-core settings file (the GTK build can
// adopt the same store later).

namespace kiwi {

// Returns the stored style, "classic" by default (unknown values fall back).
std::string load_shell_style(const std::string& config_dir);

// Persists the style; "classic" and "zen" are the accepted values.
void save_shell_style(const std::string& config_dir, const std::string& style);

// True when the value is one of the accepted style names.
bool is_valid_shell_style(const std::string& style);

}  // namespace kiwi
