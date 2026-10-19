#include "style_store.hpp"

#include <cstdio>
#include <fstream>
#include <string>

namespace kiwi {

namespace {

// Parses a single TOML key from one line, stripping quotes; returns "" when
// the key is absent. Accepts both bool (true/false) and string values.
std::string string_key(const std::string& line, const std::string& key) {
    size_t pos = line.find(key);
    if (pos == std::string::npos) return std::string();
    size_t eq = line.find('=', pos + key.size());
    if (eq == std::string::npos) return std::string();
    size_t first = line.find_first_not_of(" \t\"", eq + 1);
    if (first == std::string::npos) return std::string();
    size_t last = line.find_last_not_of(" \t\"\r");
    if (last < first) return std::string();
    return line.substr(first, last - first + 1);
}

// True unless the line is a TOML comment (the # header lines mention the
// same words the parsers look for).
bool is_comment(const std::string& line) {
    size_t start = line.find_first_not_of(" \t\r");
    return start != std::string::npos && line[start] == '#';
}

// Single writer for style.toml: every save_* rewrites the whole file with the
// current values of the other keys so they never drop each other.
void write_style_file(const std::string& config_dir, const std::string& style,
                      const std::string& backdrop, const std::string& dock) {
    FILE* out = std::fopen((config_dir + "/style.toml").c_str(), "wb");
    if (!out) return;
    std::fprintf(out, "# Kiwi shell style: classic or zen\nstyle = \"%s\"\n"
        "# Chrome backdrop: off, mica or acrylic (frosted glass; Windows 11 22H2+)\n"
        "transparency = \"%s\"\n"
        "# Docked devtools edge: bottom, left or right\n"
        "devtools_dock = \"%s\"\n",
        style.c_str(), backdrop.c_str(), dock.c_str());
    std::fclose(out);
}

}  // namespace

bool is_valid_shell_style(const std::string& style) {
    return style == "classic" || style == "zen";
}

std::string load_shell_style(const std::string& config_dir) {
    if (config_dir.empty()) return "classic";
    std::ifstream in(config_dir + "/style.toml");
    if (!in) return "classic";
    std::string line;
    while (std::getline(in, line)) {
        if (is_comment(line)) continue;
        std::string value = string_key(line, "style");
        if (value.empty()) continue;
        return is_valid_shell_style(value) ? value : "classic";
    }
    return "classic";
}

void save_shell_style(const std::string& config_dir, const std::string& style) {
    if (config_dir.empty() || !is_valid_shell_style(style)) return;
    write_style_file(config_dir, style, load_shell_backdrop(config_dir),
        load_shell_devtools_dock(config_dir));
}

bool is_valid_shell_backdrop(const std::string& backdrop) {
    return backdrop == "off" || backdrop == "mica" || backdrop == "acrylic";
}

// Legacy files stored a bare true/false; "true" meant the Mica experiment of
// 0.16.0, so it maps to mica. The default follows the style: zen starts with
// Mica, classic stays solid.
std::string load_shell_backdrop(const std::string& config_dir) {
    if (config_dir.empty()) return load_shell_style(config_dir) == "zen"
                                                     ? "mica" : "off";
    std::ifstream in(config_dir + "/style.toml");
    if (!in) return load_shell_style(config_dir) == "zen" ? "mica" : "off";
    std::string line;
    while (std::getline(in, line)) {
        if (is_comment(line)) continue;
        if (line.find("transparency") == std::string::npos) continue;
        std::string value = string_key(line, "transparency");
        if (value == "true") return "mica";
        if (value == "false") return "off";
        return is_valid_shell_backdrop(value)
            ? value : std::string(load_shell_style(config_dir) == "zen"
                                  ? "mica" : "off");
    }
    return load_shell_style(config_dir) == "zen" ? "mica" : "off";
}

void save_shell_backdrop(const std::string& config_dir, const std::string& backdrop) {
    if (config_dir.empty() || !is_valid_shell_backdrop(backdrop)) return;
    write_style_file(config_dir, load_shell_style(config_dir), backdrop,
        load_shell_devtools_dock(config_dir));
}

bool load_shell_transparency(const std::string& config_dir) {
    return load_shell_backdrop(config_dir) != "off";
}

void save_shell_transparency(const std::string& config_dir, bool enabled) {
    save_shell_backdrop(config_dir, enabled ? "mica" : "off");
}

bool is_valid_shell_devtools_dock(const std::string& dock) {
    return dock == "bottom" || dock == "left" || dock == "right";
}

std::string load_shell_devtools_dock(const std::string& config_dir) {
    if (config_dir.empty()) return "bottom";
    std::ifstream in(config_dir + "/style.toml");
    if (!in) return "bottom";
    std::string line;
    while (std::getline(in, line)) {
        if (is_comment(line)) continue;
        std::string value = string_key(line, "devtools_dock");
        if (value.empty()) continue;
        return is_valid_shell_devtools_dock(value) ? value : "bottom";
    }
    return "bottom";
}

void save_shell_devtools_dock(const std::string& config_dir, const std::string& dock) {
    if (config_dir.empty() || !is_valid_shell_devtools_dock(dock)) return;
    write_style_file(config_dir, load_shell_style(config_dir),
        load_shell_backdrop(config_dir), dock);
}

}
