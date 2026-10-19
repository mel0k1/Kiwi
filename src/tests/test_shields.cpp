// Shields unit smoke: the rules matcher and the store are plain std, so the
// test runs everywhere CTest runs. Covers the label-walk suffix matching,
// the three accepted list formats, the shields.toml round-trip and the
// per-site up/down override logic.

#include "shields_rules.hpp"
#include "shields_store.hpp"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

namespace {

int g_failures = 0;

void expect(bool ok, const std::string& what) {
    if (!ok) {
        std::cerr << "FAIL: " << what << "\n";
        ++g_failures;
    }
}

std::string dir_with(const std::string& tag) {
    static int counter = 0;
    auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    std::string dir = (std::filesystem::temp_directory_path()
                       / ("kiwi_shields_" + tag + "_" + std::to_string(stamp)
                          + "_" + std::to_string(counter++)))
                          .string();
    std::filesystem::create_directories(dir);
    return dir;
}

void write_file(const std::string& path, const std::string& text) {
    std::ofstream out(path, std::ios::binary);
    out << text;
}

}  // namespace

int main() {
    // Matching: exact host, subdomain via the label walk, miss cases.
    {
        kiwi::shields::Rules rules;
        rules.add_domain("doubleclick.net");
        rules.add_domain("analytics.google.com");
        expect(rules.hit("doubleclick.net"), "exact domain hits");
        expect(rules.hit("stats.doubleclick.net"), "subdomain hits");
        expect(rules.hit("deep.stats.doubleclick.net"), "deep subdomain hits");
        expect(rules.hit("analytics.google.com"), "multi-label rule exact");
        expect(!rules.hit("notdoubleclick.net"), "suffix lookalike misses");
        expect(!rules.hit("google.com"), "parent domain never implied");
        expect(!rules.hit(""), "empty host never hits");
        expect(rules.hit("DOUBLECLICK.NET"), "matching is case-insensitive");
        expect(rules.size() == 2, "dedupe keeps the set small");
    }

    // normalize_host: URLs, ports, userinfo, casing, whitespace.
    {
        using kiwi::shields::normalize_host;
        expect(normalize_host("https://Ads.Example.com/path?q=1") == "ads.example.com",
               "url normalized to host");
        expect(normalize_host("http://user:pass@host.example.com:8080/x") == "host.example.com",
               "userinfo and port stripped");
        expect(normalize_host("  MIXED.Case.Host  ") == "mixed.case.host",
               "trim and lowercase");
        expect(normalize_host("") .empty(), "empty stays empty");
    }

    // load_file: bare domains, hosts-file lines, easylist ||domain^ lines,
    // comments and junk are all tolerated in one file.
    {
        std::string dir = dir_with("formats");
        write_file(dir + "/list.txt",
            "# comment\n"
            "one.example\n"
            "0.0.0.0 two.example\n"
            "127.0.0.1   three.example\n"
            "||four.example^\n"
            "||five.example^$script,third-party\n"
            "@@||keepme.example^\n"
            "not_a_domain!\n"
            "\n");
        kiwi::shields::Rules rules;
        size_t parsed = rules.load_file(dir + "/list.txt");
        expect(parsed == 5, "five domains parsed");
        expect(rules.hit("two.example"), "hosts-file form loaded");
        expect(rules.hit("sub.four.example"), "easylist form loaded");
        expect(rules.hit("x.five.example"), "easylist options ignored");
        expect(!rules.hit("keepme.example"), "exceptions are dropped");
        expect(rules.load_file(dir + "/missing.txt") == 0, "missing file is safe");
    }

    // shields.toml: fresh defaults, round-trip, per-site overrides.
    {
        std::string dir = dir_with("store");
        kiwi::shields::ShieldsConfig fresh = kiwi::shields::load_shields(dir);
        expect(fresh.enabled && fresh.cosmetic && fresh.sites.empty(),
               "fresh config defaults to shields up");

        kiwi::shields::ShieldsConfig config;
        config.enabled = true;
        config.cosmetic = false;
        kiwi::shields::shields_set_site_mode(config, "News.Example.com", "down");
        kiwi::shields::shields_set_site_mode(config, "other.example", "down");
        kiwi::shields::save_shields(dir, config);

        kiwi::shields::ShieldsConfig loaded = kiwi::shields::load_shields(dir);
        expect(loaded.enabled, "enabled round-trips");
        expect(!loaded.cosmetic, "cosmetic round-trips");
        expect(loaded.sites.size() == 2, "two exceptions persisted");
        expect(kiwi::shields::shields_site_mode(loaded, "news.example.com") == "down",
               "exception read back (host normalized)");
        expect(kiwi::shields::shields_site_mode(loaded, "unknown.example") == "up",
               "unknown host stays up");

        // "up" clears the exception instead of writing a useless entry.
        kiwi::shields::shields_set_site_mode(loaded, "other.example", "up");
        expect(loaded.sites.size() == 1, "up removes the override");
        kiwi::shields::shields_set_site_mode(loaded, "news.example.com", "up");
        expect(loaded.sites.empty(), "last override removed");
    }

    // Junk values in shields.toml fall back to defaults instead of crashing.
    {
        std::string dir = dir_with("junk");
        write_file(dir + "/shields.toml",
            "enabled = banana\ncosmetic = 0\n[sites]\n\"x.example\" = \"sideways\"\n");
        kiwi::shields::ShieldsConfig junk = kiwi::shields::load_shields(dir);
        expect(junk.enabled, "junk enabled falls back to true");
        expect(junk.cosmetic, "junk cosmetic falls back to true");
        expect(junk.sites.empty(), "junk mode dropped");
    }

    if (g_failures == 0) {
        std::cout << "kiwi_shields_tests: ALL PASS\n";
        return 0;
    }
    std::cout << "kiwi_shields_tests: " << g_failures << " failures\n";
    return 1;
}
