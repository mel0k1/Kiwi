#include "workspaces_store.hpp"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <fstream>

namespace kiwi {

const char* const kWorkspaceGlyphs[] = {
    "home", "briefcase", "code", "gamepad", "cart", "news",
    "music", "mail", "star", "heart", "flame", "globe"};

namespace {

std::string trim(const std::string& text) {
    size_t begin = text.find_first_not_of(" \t\r\n");
    if (begin == std::string::npos) return std::string();
    size_t end = text.find_last_not_of(" \t\r\n");
    return text.substr(begin, end - begin + 1);
}

std::string value_after_eq(const std::string& line) {
    size_t eq = line.find('=');
    if (eq == std::string::npos) return std::string();
    std::string value = trim(line.substr(eq + 1));
    if (value.size() >= 2 && value.front() == '"' && value.back() == '"') {
        value = value.substr(1, value.size() - 2);
    }
    return value;
}

bool is_table(const std::string& line, const char* name) {
    return trim(line) == std::string("[[") + name + "]]";
}

bool is_comment(const std::string& line) {
    std::string t = trim(line);
    return !t.empty() && t[0] == '#';
}

// Reads a small 'key = [v1, v2, ...]' / 'key = v' file used by the session
// overlay; only what restore_session() needs, no full TOML parser.
std::vector<int> read_int_list(const std::string& text) {
    std::vector<int> out;
    size_t open = text.find('[');
    if (open == std::string::npos) return out;
    size_t close = text.find(']', open);
    if (close == std::string::npos) return out;
    std::string body = text.substr(open + 1, close - open - 1);
    size_t i = 0;
    while (i < body.size()) {
        if (std::isdigit(static_cast<unsigned char>(body[i]))) {
            out.push_back(std::atoi(body.c_str() + i));
            while (i < body.size()
                   && std::isdigit(static_cast<unsigned char>(body[i]))) {
                ++i;
            }
        } else {
            ++i;
        }
    }
    return out;
}

int read_int_key(const std::string& text, const char* key) {
    std::string needle = std::string(key) + " =";
    size_t pos = text.find(needle);
    if (pos == std::string::npos) return 0;
    return std::atoi(text.c_str() + pos + needle.size());
}

}  // namespace

int workspace_glyph_count() {
    return static_cast<int>(sizeof(kWorkspaceGlyphs) / sizeof(kWorkspaceGlyphs[0]));
}

bool is_valid_workspace_glyph(const std::string& glyph) {
    for (int i = 0; i < workspace_glyph_count(); ++i) {
        if (glyph == kWorkspaceGlyphs[i]) return true;
    }
    return false;
}

std::vector<WorkspaceDef> load_workspaces(const std::string& config_dir) {
    std::vector<WorkspaceDef> defs;
    if (!config_dir.empty()) {
        std::ifstream in(config_dir + "/workspaces.toml");
        if (in) {
            WorkspaceDef current;
            bool inside = false;
            std::string line;
            while (std::getline(in, line)) {
                if (is_comment(line) || trim(line).empty()) continue;
                if (is_table(line, "workspace")) {
                    if (inside && current.id > 0 && !current.name.empty()) {
                        defs.push_back(current);
                    }
                    current = WorkspaceDef();
                    inside = true;
                    continue;
                }
                if (!inside) continue;
                if (line.find("id ") != std::string::npos
                    || line.find("id=") != std::string::npos) {
                    current.id = std::atoi(value_after_eq(line).c_str());
                } else if (line.find("name ") != std::string::npos
                           || line.find("name=") != std::string::npos) {
                    current.name = value_after_eq(line);
                } else if (line.find("glyph ") != std::string::npos
                           || line.find("glyph=") != std::string::npos) {
                    current.glyph = value_after_eq(line);
                }
            }
            if (inside && current.id > 0 && !current.name.empty()) {
                defs.push_back(current);
            }
        }
    }
    // Sanitize: valid glyphs only, unique positive ids, sane names, cap at
    // kMaxWorkspaces (kept in id order — that is the rail order).
    std::vector<WorkspaceDef> clean;
    for (WorkspaceDef& def : defs) {
        if (static_cast<int>(clean.size()) >= kMaxWorkspaces) break;
        if (def.id <= 0 || def.id > 1000) continue;
        if (!is_valid_workspace_glyph(def.glyph)) def.glyph = "home";
        if (def.name.size() > 32) def.name.resize(32);
        if (def.name.empty()) continue;
        bool dup = false;
        for (const WorkspaceDef& other : clean) {
            if (other.id == def.id) dup = true;
        }
        if (!dup) clean.push_back(def);
    }
    std::sort(clean.begin(), clean.end(),
              [](const WorkspaceDef& a, const WorkspaceDef& b) {
                  return a.id < b.id;
              });
    if (clean.empty()) {
        clean.push_back(WorkspaceDef{1, "Personal", "home"});
    }
    return clean;
}

void save_workspaces(const std::string& config_dir,
                     const std::vector<WorkspaceDef>& defs) {
    if (config_dir.empty()) return;
    FILE* out = std::fopen((config_dir + "/workspaces.toml").c_str(), "wb");
    if (!out) return;
    std::fprintf(out,
        "# Kiwi workspaces: Opera-style switchable tab groups\n"
        "# glyph: home, briefcase, code, gamepad, cart, news, music, mail, "
        "star, heart, flame, globe\n");
    for (const WorkspaceDef& def : defs) {
        std::string name = def.name;
        // Keep the file parseable: strip quotes, backslashes and control chars.
        name.erase(std::remove_if(name.begin(), name.end(), [](char c) {
            return c == '"' || c == '\\' || static_cast<unsigned char>(c) < 0x20;
        }), name.end());
        if (name.empty()) name = "Workspace";
        std::fprintf(out, "\n[[workspace]]\nid = %d\nname = \"%s\"\n"
            "glyph = \"%s\"\n", def.id, name.c_str(),
            is_valid_workspace_glyph(def.glyph) ? def.glyph.c_str() : "home");
    }
    std::fclose(out);
}

int next_workspace_id(const std::vector<WorkspaceDef>& defs) {
    for (int id = 1; id <= 1000; ++id) {
        if (!workspace_id_taken(defs, id)) return id;
    }
    return 0;
}

bool workspace_id_taken(const std::vector<WorkspaceDef>& defs, int id) {
    for (const WorkspaceDef& def : defs) {
        if (def.id == id) return true;
    }
    return false;
}

void save_workspace_session(const std::string& profile_root,
                            const WorkspaceSessionState& state) {
    if (profile_root.empty()) return;
    FILE* out = std::fopen((profile_root + "/workspace-session.toml").c_str(), "wb");
    if (!out) return;
    std::fprintf(out,
        "# Tab->workspace mapping, aligned 1:1 with session.toml tab order\n"
        "workspaces = [");
    for (size_t i = 0; i < state.tab_workspaces.size(); ++i) {
        if (i) std::fprintf(out, ", ");
        std::fprintf(out, "%d", state.tab_workspaces[i]);
    }
    std::fprintf(out, "]\nactive = %d\n", state.active_workspace);
    std::fclose(out);
}

bool load_workspace_session(const std::string& profile_root,
                            WorkspaceSessionState& out) {
    if (profile_root.empty()) return false;
    std::ifstream in(profile_root + "/workspace-session.toml");
    if (!in) return false;
    std::string body((std::istreambuf_iterator<char>(in)),
                     std::istreambuf_iterator<char>());
    out.tab_workspaces = read_int_list(body);
    int active = read_int_key(body, "active");
    out.active_workspace = active > 0 ? active : 1;
    return !out.tab_workspaces.empty();
}

}

