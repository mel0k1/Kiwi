#include "style_store.hpp"

#include <cstdio>
#include <fstream>
#include <string>

namespace kiwi {

bool is_valid_shell_style(const std::string& style) {
    return style == "classic" || style == "zen";
}

std::string load_shell_style(const std::string& config_dir) {
    if (config_dir.empty()) return "classic";
    std::ifstream in(config_dir + "/style.toml");
    if (!in) return "classic";
    std::string line;
    while (std::getline(in, line)) {
        // Accept: style = "zen" / style = 'zen' / style = zen
        const std::string key = "style";
        size_t pos = line.find(key);
        if (pos == std::string::npos) continue;
        size_t eq = line.find('=', pos + key.size());
        if (eq == std::string::npos) continue;
        size_t first = line.find_first_not_of(" \t\"", eq + 1);
        if (first == std::string::npos) continue;
        size_t last = line.find_last_not_of(" \t\"\r");
        if (last < first) continue;
        std::string value = line.substr(first, last - first + 1);
        return is_valid_shell_style(value) ? value : "classic";
    }
    return "classic";
}

void save_shell_style(const std::string& config_dir, const std::string& style) {
    if (config_dir.empty() || !is_valid_shell_style(style)) return;
    FILE* out = std::fopen((config_dir + "/style.toml").c_str(), "wb");
    if (!out) return;
    std::fprintf(out, "# Kiwi shell style: classic or zen\nstyle = \"%s\"\n",
        style.c_str());
    std::fclose(out);
}

}  // namespace kiwi
