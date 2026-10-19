// Workspaces store round-trip smoke: workspaces.toml parsing/sanitizing, the
// id helpers and the workspace-session.toml overlay. Plain std, runs
// everywhere CTest runs.

#include "workspaces_store.hpp"

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
                       / ("kiwi_workspaces_" + tag + "_" + std::to_string(stamp)
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
    using kiwi::WorkspaceDef;

    // Fresh install: exactly one default workspace.
    {
        std::string dir = dir_with("fresh");
        std::vector<WorkspaceDef> defs = kiwi::load_workspaces(dir);
        expect(defs.size() == 1, "default is a single workspace");
        expect(defs[0].id == 1 && defs[0].name == "Personal" && defs[0].glyph == "home",
               "default workspace is Personal/home");
        expect(kiwi::next_workspace_id(defs) == 2, "next id after default");
    }

    // Round-trip through save + load, id helpers, glyph validation.
    {
        std::string dir = dir_with("roundtrip");
        std::vector<WorkspaceDef> defs = {
            WorkspaceDef{1, "Personal", "home"},
            WorkspaceDef{4, "Work", "briefcase"},
            WorkspaceDef{7, "Fun", "gamepad"},
        };
        kiwi::save_workspaces(dir, defs);
        std::vector<WorkspaceDef> loaded = kiwi::load_workspaces(dir);
        expect(loaded.size() == 3, "all three persisted");
        expect(loaded[1].id == 4 && loaded[1].name == "Work"
               && loaded[1].glyph == "briefcase", "middle entry intact");
        expect(loaded[0].id < loaded[1].id && loaded[1].id < loaded[2].id,
               "sorted by id after load");
        expect(kiwi::next_workspace_id(loaded) == 2, "next free id is the gap");
        expect(kiwi::workspace_id_taken(loaded, 7), "id 7 taken");
        expect(!kiwi::workspace_id_taken(loaded, 8), "id 8 free");
        expect(kiwi::is_valid_workspace_glyph("flame")
               && !kiwi::is_valid_workspace_glyph("rocket"), "glyph validation");
    }

    // Sanitizing: junk glyphs fall back to home, broken entries drop,
    // duplicates collapse, the count is capped at kMaxWorkspaces.
    {
        std::string dir = dir_with("sanitize");
        write_file(dir + "/workspaces.toml",
            "[[workspace]]\nid = 3\nname = \"Bad glyph\"\nglyph = \"rocket\"\n"
            "[[workspace]]\nid = 0\nname = \"No id\"\nglyph = \"home\"\n"
            "[[workspace]]\nid = 5\nname = \"Dup\"\nglyph = \"star\"\n"
            "[[workspace]]\nid = 5\nname = \"Dup again\"\nglyph = \"mail\"\n"
            "[[workspace]]\nid = 9\nname = \"\"\nglyph = \"cart\"\n");
        std::vector<WorkspaceDef> loaded = kiwi::load_workspaces(dir);
        expect(loaded.size() == 2, "broken and empty entries dropped, dup collapsed");
        expect(loaded[0].glyph == "home", "unknown glyph falls back to home");
        expect(loaded[1].name == "Dup", "first duplicate wins");
    }

    // The cap holds even with more entries in the file.
    {
        std::string dir = dir_with("cap");
        std::string body;
        for (int i = 1; i <= 12; ++i) {
            body += "[[workspace]]\nid = " + std::to_string(i)
                + "\nname = \"W" + std::to_string(i) + "\"\nglyph = \"home\"\n";
        }
        write_file(dir + "/workspaces.toml", body);
        std::vector<WorkspaceDef> loaded = kiwi::load_workspaces(dir);
        expect(static_cast<int>(loaded.size()) == kiwi::kMaxWorkspaces,
               "workspace count capped");
    }

    // Session overlay round-trip.
    {
        std::string dir = dir_with("session");
        kiwi::WorkspaceSessionState state;
        state.tab_workspaces = {1, 1, 4, 4, 4, 2};
        state.active_workspace = 4;
        kiwi::save_workspace_session(dir, state);
        kiwi::WorkspaceSessionState loaded;
        expect(kiwi::load_workspace_session(dir, loaded), "overlay loads back");
        expect(loaded.tab_workspaces.size() == 6 && loaded.tab_workspaces[2] == 4,
               "mapping intact");
        expect(loaded.active_workspace == 4, "active workspace intact");
    }

    // Missing overlay: load fails, caller keeps the default.
    {
        std::string dir = dir_with("missing");
        kiwi::WorkspaceSessionState loaded;
        expect(!kiwi::load_workspace_session(dir, loaded), "missing overlay fails");
    }

    if (g_failures == 0) {
        std::cout << "kiwi_workspaces_tests: ALL PASS\n";
        return 0;
    }
    std::cout << "kiwi_workspaces_tests: " << g_failures << " failures\n";
    return 1;
}
