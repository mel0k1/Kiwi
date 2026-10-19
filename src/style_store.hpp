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

// Chrome backdrop behind the painted chrome: "off" (solid), "mica" or
// "acrylic" (frosted glass). The renderer falls back to opaque chrome
// pre-Windows 11 22H2 either way. Legacy files stored a bare true/false for
// the same key (true maps to mica); the default follows the style: zen
// starts with mica, classic stays solid.
std::string load_shell_backdrop(const std::string& config_dir);
void save_shell_backdrop(const std::string& config_dir, const std::string& backdrop);
bool is_valid_shell_backdrop(const std::string& backdrop);

// Boolean view over the backdrop ("off" or not); kept for callers that only
// ask "is the chrome translucent".
bool load_shell_transparency(const std::string& config_dir);
void save_shell_transparency(const std::string& config_dir, bool enabled);

// Docked DevTools side: "bottom" (default), "left" or "right".
std::string load_shell_devtools_dock(const std::string& config_dir);
void save_shell_devtools_dock(const std::string& config_dir, const std::string& dock);
bool is_valid_shell_devtools_dock(const std::string& dock);

// Chrome color theme: "auto" (follow the Windows app mode, default), "light",
// "dark" or one of the ready-made palettes ("nord", "dracula", "catppuccin",
// "solarized", "sakura", "crimson"). Unknown values fall back to "auto".
std::string load_shell_theme(const std::string& config_dir);
void save_shell_theme(const std::string& config_dir, const std::string& theme);
bool is_valid_shell_theme(const std::string& theme);

// Accent color override over the theme palette: "" (theme default) or a
// "#RRGGBB" hex string. Anything else is rejected on save and reads as "".
std::string load_shell_accent(const std::string& config_dir);
void save_shell_accent(const std::string& config_dir, const std::string& accent);
bool is_valid_shell_accent(const std::string& accent);

// Side of the Zen tab rail: "left" (default) or "right".
std::string load_shell_rail_side(const std::string& config_dir);
void save_shell_rail_side(const std::string& config_dir, const std::string& side);
bool is_valid_shell_rail_side(const std::string& side);

}
