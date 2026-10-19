// style_store round-trip smoke: the store is plain std (no Windows/GTK), so
// this test runs everywhere CTest runs and guards the style.toml contract —
// valid values round-trip, junk falls back, legacy files read correctly and
// no save_* ever drops another key.

#include "style_store.hpp"

#include <cstdio>
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

std::string read_file(const std::string& dir) {
    std::ifstream in(dir + "/style.toml");
    std::string all;
    std::string line;
    while (std::getline(in, line)) {
        all += line;
        all += "\n";
    }
    return all;
}

void write_file(const std::string& dir, const std::string& text) {
    std::ofstream out(dir + "/style.toml", std::ios::binary);
    out << text;
}

std::string dir_with(const std::string& tag) {
    static int counter = 0;
    auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    std::string dir = (std::filesystem::temp_directory_path()
                       / ("kiwi_style_store_" + tag + "_" + std::to_string(stamp)
                          + "_" + std::to_string(counter++)))
                          .string();
    std::filesystem::create_directories(dir);
    return dir;
}

}  // namespace

int main() {
    // Fresh install: every load returns its documented default.
    {
        std::string dir = dir_with("fresh");
        expect(kiwi::load_shell_style(dir) == "classic", "default style");
        expect(kiwi::load_shell_backdrop(dir) == "off", "default backdrop");
        expect(kiwi::load_shell_devtools_dock(dir) == "bottom", "default devtools dock");
        expect(kiwi::load_shell_theme(dir) == "auto", "default theme");
        expect(kiwi::load_shell_accent(dir).empty(), "default accent");
        expect(kiwi::load_shell_rail_side(dir) == "left", "default rail side");
    }

    // Round-trips: valid values persist and no key drops another.
    {
        std::string dir = dir_with("roundtrip");
        kiwi::save_shell_style(dir, "zen");
        kiwi::save_shell_backdrop(dir, "acrylic");
        kiwi::save_shell_devtools_dock(dir, "left");
        kiwi::save_shell_theme(dir, "nord");
        kiwi::save_shell_accent(dir, "#88C0D0");
        kiwi::save_shell_rail_side(dir, "right");

        expect(kiwi::load_shell_style(dir) == "zen", "style roundtrip");
        expect(kiwi::load_shell_backdrop(dir) == "acrylic", "backdrop roundtrip");
        expect(kiwi::load_shell_devtools_dock(dir) == "left", "dock roundtrip");
        expect(kiwi::load_shell_theme(dir) == "nord", "theme roundtrip");
        expect(kiwi::load_shell_accent(dir) == "#88C0D0", "accent roundtrip");
        expect(kiwi::load_shell_rail_side(dir) == "right", "rail side roundtrip");

        // style.toml must mention all six keys (the file carries them all).
        std::string text = read_file(dir);
        for (const char* key : {"style", "transparency", "devtools_dock",
                                "theme", "accent", "rail_side"}) {
            expect(text.find(key) != std::string::npos,
                   std::string("file keeps key ") + key);
        }

        // A late save of one key keeps every other key.
        kiwi::save_shell_style(dir, "classic");
        expect(kiwi::load_shell_backdrop(dir) == "acrylic", "backdrop survives style save");
        expect(kiwi::load_shell_theme(dir) == "nord", "theme survives style save");
        expect(kiwi::load_shell_accent(dir) == "#88C0D0", "accent survives style save");
        expect(kiwi::load_shell_rail_side(dir) == "right", "rail survives style save");
        expect(kiwi::load_shell_devtools_dock(dir) == "left", "dock survives style save");
    }

    // Junk values fall back instead of poisoning the shell.
    {
        std::string dir = dir_with("junk");
        write_file(dir, "style = \"bloated\"\ntransparency = \"glass\"\n"
                        "devtools_dock = \"up\"\ntheme = \"win95\"\n"
                        "accent = \"orange\"\nrail_side = \"center\"\n");
        expect(kiwi::load_shell_style(dir) == "classic", "junk style -> classic");
        expect(kiwi::load_shell_backdrop(dir) == "off", "junk backdrop -> off");
        expect(kiwi::load_shell_devtools_dock(dir) == "bottom", "junk dock -> bottom");
        expect(kiwi::load_shell_theme(dir) == "auto", "junk theme -> auto");
        expect(kiwi::load_shell_accent(dir).empty(), "junk accent -> empty");
        expect(kiwi::load_shell_rail_side(dir) == "left", "junk rail -> left");
    }

    // Legacy 0.16.0 files stored a bare true/false under transparency.
    {
        std::string dir = dir_with("legacy");
        write_file(dir, "style = \"zen\"\ntransparency = true\n");
        expect(kiwi::load_shell_backdrop(dir) == "mica", "legacy true -> mica");
        write_file(dir, "style = \"classic\"\ntransparency = false\n");
        expect(kiwi::load_shell_backdrop(dir) == "off", "legacy false -> off");
    }

    // Validation gates: saves of invalid values are no-ops, accent needs hex.
    {
        std::string dir = dir_with("validate");
        kiwi::save_shell_style(dir, "zen");
        kiwi::save_shell_theme(dir, "macos");           // not a theme
        expect(kiwi::load_shell_theme(dir) == "auto", "invalid theme rejected");
        kiwi::save_shell_accent(dir, "FF0000");          // no '#'
        kiwi::save_shell_accent(dir, "#GG0000");         // not hex
        kiwi::save_shell_accent(dir, "#FF00");           // too short
        expect(kiwi::load_shell_accent(dir).empty(), "invalid accents rejected");
        kiwi::save_shell_accent(dir, "#ff00ff");         // lowercase hex is fine
        expect(kiwi::load_shell_accent(dir) == "#ff00ff", "lowercase accent kept");
        kiwi::save_shell_rail_side(dir, "top");          // not a side
        expect(kiwi::load_shell_rail_side(dir) == "left", "invalid rail rejected");
        expect(kiwi::load_shell_style(dir) == "zen", "style survived rejections");
    }

    if (g_failures == 0) {
        std::cout << "style_store smoke: all cases passed\n";
        return 0;
    }
    std::cerr << "style_store smoke: " << g_failures << " failure(s)\n";
    return 1;
}
