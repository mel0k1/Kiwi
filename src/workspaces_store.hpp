#pragma once

#include <string>
#include <vector>

// Kiwi workspaces — Opera-style switchable tab groups, stored Kiwi-side in
// <config>/workspaces.toml (shell feature; the GTK build ignores it for now
// and keeps its single implicit workspace). The tab->workspace mapping is
// session state, persisted separately next to session.toml so the restore
// order stays aligned with the mustela-core session file.

namespace kiwi {

struct WorkspaceDef {
    int id = 0;              // stable identity, assigned from a free counter
    std::string name;        // shown on the rail pill and in the manager
    std::string glyph;       // one of kWorkspaceGlyphs
};

// Upper bound: the rail section is sized for this many pills.
constexpr int kMaxWorkspaces = 8;

// Glyph names accepted in workspaces.toml and offered in the manager combo.
extern const char* const kWorkspaceGlyphs[];
int workspace_glyph_count();
bool is_valid_workspace_glyph(const std::string& glyph);

// Loads the workspace list; a missing or unreadable file yields the single
// default workspace {id=1, "Personal", "home"}. Unknown glyphs fall back to
// "home", broken entries are dropped, ids are de-duplicated and kept sorted.
std::vector<WorkspaceDef> load_workspaces(const std::string& config_dir);

// Persists the whole list (single writer, whole-file rewrite).
void save_workspaces(const std::string& config_dir,
                     const std::vector<WorkspaceDef>& defs);

// Lowest positive id not used yet.
int next_workspace_id(const std::vector<WorkspaceDef>& defs);

// True when |defs| already holds a workspace with this id.
bool workspace_id_taken(const std::vector<WorkspaceDef>& defs, int id);

// Tab->workspace session overlay (profile_root/workspace-session.toml):
// |tab_workspaces| is aligned 1:1 with session.toml's tab order.
struct WorkspaceSessionState {
    std::vector<int> tab_workspaces;
    int active_workspace = 1;
};

void save_workspace_session(const std::string& profile_root,
                            const WorkspaceSessionState& state);
bool load_workspace_session(const std::string& profile_root,
                            WorkspaceSessionState& out);
}

