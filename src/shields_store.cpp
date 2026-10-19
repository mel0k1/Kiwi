#include "shields_store.hpp"

#include <cstdio>
#include <fstream>

#include "shields_rules.hpp"

namespace kiwi {
namespace shields {

namespace {

std::string trim(const std::string& text) {
    size_t begin = text.find_first_not_of(" \t\r\n");
    if (begin == std::string::npos) return std::string();
    size_t end = text.find_last_not_of(" \t\r\n");
    return text.substr(begin, end - begin + 1);
}

// Pulls the value out of 'key = value', stripping quotes.
std::string value_after_eq(const std::string& line, const std::string& key) {
    size_t pos = line.find(key);
    if (pos == std::string::npos) return std::string();
    size_t eq = line.find('=', pos + key.size());
    if (eq == std::string::npos) return std::string();
    std::string value = trim(line.substr(eq + 1));
    if (value.size() >= 2 && value.front() == '"' && value.back() == '"') {
        value = value.substr(1, value.size() - 2);
    }
    return value;
}

std::string quoted_key(const std::string& line) {
    size_t first = line.find('"');
    if (first == std::string::npos) return std::string();
    size_t last = line.find('"', first + 1);
    if (last == std::string::npos) return std::string();
    return line.substr(first + 1, last - first - 1);
}

bool in_sites_table(const std::string& line) {
    return trim(line) == "[sites]";
}

bool is_comment(const std::string& line) {
    std::string t = trim(line);
    return !t.empty() && t[0] == '#';
}

}  // namespace

ShieldsConfig load_shields(const std::string& config_dir) {
    ShieldsConfig config;
    if (config_dir.empty()) return config;
    std::ifstream in(config_dir + "/shields.toml");
    if (!in) return config;
    bool sites = false;
    std::string line;
    while (std::getline(in, line)) {
        if (is_comment(line) || trim(line).empty()) continue;
        if (in_sites_table(line)) {
            sites = true;
            continue;
        }
        if (sites) {
            std::string host = quoted_key(line);
            std::string mode = value_after_eq(line, host);
            if (!host.empty() && (mode == "up" || mode == "down")) {
                config.sites.emplace_back(host, mode);
            }
            continue;
        }
        std::string value;
        value = value_after_eq(line, "enabled");
        if (value == "true") config.enabled = true;
        if (value == "false") config.enabled = false;
        value = value_after_eq(line, "cosmetic");
        if (value == "true") config.cosmetic = true;
        if (value == "false") config.cosmetic = false;
    }
    return config;
}

void save_shields(const std::string& config_dir, const ShieldsConfig& config) {
    if (config_dir.empty()) return;
    FILE* out = std::fopen((config_dir + "/shields.toml").c_str(), "wb");
    if (!out) return;
    std::fprintf(out,
        "# Kiwi Shields: network ad/tracker blocking and cosmetic filtering\n"
        "enabled = %s\n"
        "cosmetic = %s\n"
        "\n[sites]\n"
        "# \"host\" = \"down\" lowers shields for that site; \"up\" re-raises them\n",
        config.enabled ? "true" : "false",
        config.cosmetic ? "true" : "false");
    for (const auto& site : config.sites) {
        std::string host = site.first;
        std::string mode = site.second == "down" ? "down" : "up";
        bool safe = true;
        for (char c : host) {
            if (c == '"' || c == '\\' || static_cast<unsigned char>(c) < 0x20
                || static_cast<unsigned char>(c) > 0x7e) {
                safe = false;
                break;
            }
        }
        if (safe && !host.empty()) {
            std::fprintf(out, "\"%s\" = \"%s\"\n", host.c_str(), mode.c_str());
        }
    }
    std::fclose(out);
}

std::string shields_site_mode(const ShieldsConfig& config, const std::string& host) {
    std::string clean = normalize_host(host);
    for (const auto& site : config.sites) {
        if (site.first == clean) return site.second == "down" ? "down" : "up";
    }
    return "up";
}

void shields_set_site_mode(ShieldsConfig& config, const std::string& host,
                           const std::string& mode) {
    std::string clean = normalize_host(host);
    if (clean.empty()) return;
    for (size_t i = 0; i < config.sites.size(); ++i) {
        if (config.sites[i].first != clean) continue;
        if (mode == "down") {
            config.sites[i].second = "down";
        } else {
            // "up" clears the exception instead of keeping a useless entry.
            config.sites.erase(config.sites.begin() + static_cast<long>(i));
        }
        return;
    }
    if (mode == "down") config.sites.emplace_back(clean, "down");
}

}

}
