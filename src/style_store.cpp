#include "style_store.hpp"

#include <cstdio>
#include <fstream>
#include <string>

namespace kiwi {

namespace {

// Parses a string value like style = "zen" from a single TOML line.
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
        std::string value = string_key(line, "style");
        if (value.empty()) continue;
        return is_valid_shell_style(value) ? value : "classic";
    }
    return "classic";
}

void save_shell_style(const std::string& config_dir, const std::string& style) {
    if (config_dir.empty() || !is_valid_shell_style(style)) return;
    std::ifstream in(config_dir + "/style.toml");
    bool transparency = style == "zen" && load_shell_transparency(config_dir);
    if (in) {
        std::string line;
        while (std::getline(in, line)) {
            if (line.find("transparency") != std::string::npos) {
                transparency = line.find("true") != std::string::npos;
            }
        }
    }
    FILE* out = std::fopen((config_dir + "/style.toml").c_str(), "wb");
    if (!out) return;
    std::fprintf(out, "# Kiwi shell style: classic or zen\nstyle = \"%s\"\n"
        "# Mica/acrylic chrome transparency (Windows 11 22H2+)\n"
        "transparency = %s\n",
        style.c_str(), transparency ? "true" : "false");
    std::fclose(out);
}

bool load_shell_transparency(const std::string& config_dir) {
    if (config_dir.empty()) return false;
    std::ifstream in(config_dir + "/style.toml");
    if (!in) return load_shell_style(config_dir) == "zen";
    std::string line;
    while (std::getline(in, line)) {
        if (line.find("transparency") == std::string::npos) continue;
        return line.find("true") != std::string::npos;
    }
    return load_shell_style(config_dir) == "zen";
}

void save_shell_transparency(const std::string& config_dir, bool enabled) {
    if (config_dir.empty()) return;
    std::string style = load_shell_style(config_dir);
    FILE* out = std::fopen((config_dir + "/style.toml").c_str(), "wb");
    if (!out) return;
    std::fprintf(out, "# Kiwi shell style: classic or zen\nstyle = \"%s\"\n"
        "# Mica/acrylic chrome transparency (Windows 11 22H2+)\n"
        "transparency = %s\n",
        style.c_str(), enabled ? "true" : "false");
    std::fclose(out);
}

}
