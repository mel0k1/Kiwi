#include "browser.hpp"

#include <glib/gstdio.h>

#include <cctype>
#include <cstdint>
#include <string>
#include <vector>

#include "mustela/mustela.hpp"
#include "updater.hpp"

// ---------------- Settings (K3): search engines + profiles ----------------

namespace {

enum EngineCols {
    EC_KEYWORD,
    EC_NAME,
    EC_TEMPLATE,
    EC_DEFAULT,
    EC_N
};

enum ProfileCols {
    PC_STATE,
    PC_NAME,
    PC_ID,
    PC_ROOT,
    PC_N
};

enum ImportCols {
    IC_BROWSER,
    IC_PROFILE,
    IC_PATH,
    IC_N
};

struct PrefsPage {
    Browser* b = nullptr;
    GtkListStore* engines_store = nullptr;
    GtkWidget* engines_tree = nullptr;
    GtkListStore* profiles_store = nullptr;
    GtkWidget* profiles_tree = nullptr;
    GtkListStore* import_store = nullptr;
    GtkWidget* import_tree = nullptr;
    GtkWidget* import_status = nullptr;
    GtkWidget* updates_status = nullptr;
    GtkWidget* updates_notes = nullptr;
    GtkWidget* updates_check_btn = nullptr;
    GtkWidget* updates_download_btn = nullptr;
    GtkWidget* updates_open_btn = nullptr;
};

PrefsPage g_prefs;

std::string trim_copy(const std::string& in) {
    size_t start = 0;
    size_t end = in.size();
    while (start < end && std::isspace(static_cast<unsigned char>(in[start]))) ++start;
    while (end > start && std::isspace(static_cast<unsigned char>(in[end - 1]))) --end;
    return in.substr(start, end - start);
}

std::string to_lower_ascii(const std::string& s) {
    std::string out = s;
    for (char& c : out) {
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    }
    return out;
}

std::string slugify(const std::string& name) {
    std::string out;
    for (char c : name) {
        if (std::isalnum(static_cast<unsigned char>(c))) {
            out += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        } else if (!out.empty() && out.back() != '-') {
            out += '-';
        }
    }
    while (!out.empty() && out.back() == '-') out.pop_back();
    return out;
}

std::string config_file_path(const char* name) {
    gchar* path = g_build_filename(g_get_user_config_dir(), "kiwi", name, nullptr);
    std::string out = path ? path : "";
    g_free(path);
    return out;
}

std::string selected_row_text(GtkWidget* tree, int column) {
    if (!tree) return std::string();
    GtkTreeSelection* selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(tree));
    GtkTreeModel* model = nullptr;
    GtkTreeIter iter;
    if (!gtk_tree_selection_get_selected(selection, &model, &iter)) return std::string();
    gchar* text = nullptr;
    gtk_tree_model_get(model, &iter, column, &text, -1);
    std::string out = text ? text : "";
    g_free(text);
    return out;
}

// ---- search engines ----

void refresh_engines_list() {
    if (!g_prefs.engines_store || !g_prefs.b) return;
    gtk_list_store_clear(g_prefs.engines_store);
    const Browser& b = *g_prefs.b;
    for (const mustela::SearchEngine& engine : b.settings.search_engines) {
        GtkTreeIter iter;
        gtk_list_store_append(g_prefs.engines_store, &iter);
        gtk_list_store_set(g_prefs.engines_store, &iter,
            EC_KEYWORD, engine.keyword.c_str(),
            EC_NAME, engine.name.c_str(),
            EC_TEMPLATE, engine.url_template.c_str(),
            EC_DEFAULT, to_lower_ascii(engine.keyword) == to_lower_ascii(b.settings.search_default)
                ? "default" : "", -1);
    }
}

struct EngineEditor {
    Browser* b = nullptr;
    GtkWidget* dialog = nullptr;
    GtkWidget* keyword = nullptr;
    GtkWidget* name = nullptr;
    GtkWidget* tmpl = nullptr;
    GtkWidget* hint = nullptr;
    std::string original_keyword;
};

void on_engine_editor_destroy(GtkWidget*, gpointer user_data) {
    delete static_cast<EngineEditor*>(user_data);
}

void on_engine_editor_response(GtkDialog* dialog, gint response, gpointer user_data) {
    EngineEditor* e = static_cast<EngineEditor*>(user_data);
    if (response == GTK_RESPONSE_OK && e && e->b) {
        std::string keyword = trim_copy(gtk_entry_get_text(GTK_ENTRY(e->keyword)));
        std::string name = trim_copy(gtk_entry_get_text(GTK_ENTRY(e->name)));
        std::string tmpl = trim_copy(gtk_entry_get_text(GTK_ENTRY(e->tmpl)));
        if (keyword.empty() || tmpl.empty()) {
            gtk_label_set_text(GTK_LABEL(e->hint), "Keyword and template are required.");
            return;
        }
        if (tmpl.find("%s") == std::string::npos) {
            gtk_label_set_text(GTK_LABEL(e->hint),
                "Template must contain %s where the URL-encoded query goes.");
            return;
        }
        for (const mustela::SearchEngine& engine : e->b->settings.search_engines) {
            if (to_lower_ascii(engine.keyword) == to_lower_ascii(e->original_keyword)) continue;
            if (to_lower_ascii(engine.keyword) == to_lower_ascii(keyword)) {
                gtk_label_set_text(GTK_LABEL(e->hint), "Keyword is already used by another engine.");
                return;
            }
        }
        mustela::SearchEngine engine;
        engine.keyword = keyword;
        engine.name = name.empty() ? keyword : name;
        engine.url_template = tmpl;
        std::vector<mustela::SearchEngine>& engines = e->b->settings.search_engines;
        if (!e->original_keyword.empty()) {
            for (auto it = engines.begin(); it != engines.end(); ++it) {
                if (to_lower_ascii(it->keyword) == to_lower_ascii(e->original_keyword)) {
                    engines.erase(it);
                    break;
                }
            }
        }
        bool replaced = false;
        for (mustela::SearchEngine& existing : engines) {
            if (to_lower_ascii(existing.keyword) == to_lower_ascii(keyword)) {
                existing = engine;
                replaced = true;
                break;
            }
        }
        if (!replaced) engines.push_back(engine);
        std::string& def = e->b->settings.search_default;
        if (def.empty() || to_lower_ascii(def) == to_lower_ascii(e->original_keyword)) def = keyword;
        mustela::save_settings(config_file_path("settings.toml"), e->b->settings);
        refresh_engines_list();
        flash_status(*e->b, "search engines updated");
    }
    gtk_widget_destroy(GTK_WIDGET(dialog));
}

void open_engine_editor(Browser& b, const mustela::SearchEngine* existing) {
    EngineEditor* e = new EngineEditor;
    e->b = &b;
    if (existing) e->original_keyword = existing->keyword;
    e->dialog = gtk_dialog_new_with_buttons(
        existing ? "Edit search engine — Kiwi" : "Add search engine — Kiwi",
        b.settings_window ? GTK_WINDOW(b.settings_window) : GTK_WINDOW(b.window),
        static_cast<GtkDialogFlags>(GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT),
        "_Cancel", GTK_RESPONSE_CANCEL,
        "_Save", GTK_RESPONSE_OK,
        nullptr);
    GtkWidget* save_btn = gtk_dialog_get_widget_for_response(GTK_DIALOG(e->dialog), GTK_RESPONSE_OK);
    if (save_btn) gtk_style_context_add_class(gtk_widget_get_style_context(save_btn), "kiwi-accent");
    g_signal_connect(e->dialog, "destroy", G_CALLBACK(on_engine_editor_destroy), e);
    g_signal_connect(e->dialog, "response", G_CALLBACK(on_engine_editor_response), e);

    GtkWidget* grid = gtk_grid_new();
    gtk_grid_set_row_spacing(GTK_GRID(grid), 4);
    gtk_grid_set_column_spacing(GTK_GRID(grid), 8);
    gtk_container_set_border_width(GTK_CONTAINER(grid), 8);

    const auto attach_row = [&grid](int row, const char* text, GtkWidget* widget) {
        GtkWidget* label = gtk_label_new(text);
        gtk_label_set_xalign(GTK_LABEL(label), 0.0f);
        gtk_grid_attach(GTK_GRID(grid), label, 0, row, 1, 1);
        gtk_grid_attach(GTK_GRID(grid), widget, 1, row, 1, 1);
    };

    e->keyword = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(e->keyword), "g — used as !g query");
    if (existing) gtk_entry_set_text(GTK_ENTRY(e->keyword), existing->keyword.c_str());
    attach_row(0, "Keyword", e->keyword);

    e->name = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(e->name), "DuckDuckGo (optional)");
    if (existing) gtk_entry_set_text(GTK_ENTRY(e->name), existing->name.c_str());
    attach_row(1, "Name", e->name);

    e->tmpl = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(e->tmpl), "https://duckduckgo.com/?q=%s");
    if (existing) gtk_entry_set_text(GTK_ENTRY(e->tmpl), existing->url_template.c_str());
    attach_row(2, "URL template", e->tmpl);

    e->hint = gtk_label_new("%s in the template is replaced by the URL-encoded query.");
    gtk_style_context_add_class(gtk_widget_get_style_context(e->hint), "kiwi-secondary");
    gtk_label_set_xalign(GTK_LABEL(e->hint), 0.0f);

    GtkWidget* content = gtk_dialog_get_content_area(GTK_DIALOG(e->dialog));
    gtk_box_pack_start(GTK_BOX(content), grid, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(content), e->hint, FALSE, FALSE, 4);

    gtk_widget_show_all(e->dialog);
}

void on_engine_add(GtkButton*, gpointer) {
    if (!g_prefs.b) return;
    open_engine_editor(*g_prefs.b, nullptr);
}

void on_engine_edit(GtkButton*, gpointer) {
    if (!g_prefs.b) return;
    std::string keyword = selected_row_text(g_prefs.engines_tree, EC_KEYWORD);
    if (keyword.empty()) return;
    for (const mustela::SearchEngine& engine : g_prefs.b->settings.search_engines) {
        if (engine.keyword == keyword) {
            open_engine_editor(*g_prefs.b, &engine);
            return;
        }
    }
}

void on_engine_row_activated(GtkTreeView*, GtkTreePath*, GtkTreeViewColumn*, gpointer) {
    on_engine_edit(nullptr, nullptr);
}

void on_engine_remove(GtkButton*, gpointer) {
    if (!g_prefs.b) return;
    std::string keyword = selected_row_text(g_prefs.engines_tree, EC_KEYWORD);
    if (keyword.empty()) return;
    std::vector<mustela::SearchEngine>& engines = g_prefs.b->settings.search_engines;
    for (auto it = engines.begin(); it != engines.end(); ++it) {
        if (to_lower_ascii(it->keyword) == to_lower_ascii(keyword)) {
            engines.erase(it);
            break;
        }
    }
    if (to_lower_ascii(g_prefs.b->settings.search_default) == to_lower_ascii(keyword)) {
        g_prefs.b->settings.search_default.clear();
    }
    mustela::save_settings(config_file_path("settings.toml"), g_prefs.b->settings);
    refresh_engines_list();
    flash_status(*g_prefs.b, "search engine removed");
}

void on_engine_default(GtkButton*, gpointer) {
    if (!g_prefs.b) return;
    std::string keyword = selected_row_text(g_prefs.engines_tree, EC_KEYWORD);
    if (keyword.empty()) return;
    g_prefs.b->settings.search_default = keyword;
    mustela::save_settings(config_file_path("settings.toml"), g_prefs.b->settings);
    refresh_engines_list();
    flash_status(*g_prefs.b, "default search engine: " + keyword);
}

// ---- profiles ----

void refresh_profiles_list() {
    if (!g_prefs.profiles_store || !g_prefs.b) return;
    gtk_list_store_clear(g_prefs.profiles_store);
    mustela::ProfileRegistry registry = mustela::load_profile_registry(config_file_path("profiles.toml"));
    for (const mustela::ProfileEntry& entry : registry.entries) {
        std::string state;
        if (entry.id == g_prefs.b->settings.shell_profile) state = "\xE2\x97\x8F active";
        else if (entry.id == registry.default_id) state = "default";
        GtkTreeIter iter;
        gtk_list_store_append(g_prefs.profiles_store, &iter);
        gtk_list_store_set(g_prefs.profiles_store, &iter,
            PC_STATE, state.c_str(),
            PC_NAME, entry.name.c_str(),
            PC_ID, entry.id.c_str(),
            PC_ROOT, entry.root.c_str(), -1);
    }
}

struct ProfileEditor {
    Browser* b = nullptr;
    GtkWidget* dialog = nullptr;
    GtkWidget* name = nullptr;
    GtkWidget* hint = nullptr;
    std::string original_id;
};

void on_profile_editor_destroy(GtkWidget*, gpointer user_data) {
    delete static_cast<ProfileEditor*>(user_data);
}

void on_profile_editor_response(GtkDialog* dialog, gint response, gpointer user_data) {
    ProfileEditor* e = static_cast<ProfileEditor*>(user_data);
    if (response == GTK_RESPONSE_OK && e && e->b) {
        std::string name = trim_copy(gtk_entry_get_text(GTK_ENTRY(e->name)));
        if (name.empty()) {
            gtk_label_set_text(GTK_LABEL(e->hint), "Name is required.");
            return;
        }
        std::string path = config_file_path("profiles.toml");
        mustela::ProfileRegistry registry = mustela::load_profile_registry(path);
        if (e->original_id.empty()) {
            std::string id = slugify(name);
            if (id.empty()) id = "profile";
            std::string base = id;
            int n = 2;
            while (registry.find(id)) id = base + "-" + std::to_string(n++);
            mustela::ProfileEntry entry;
            entry.id = id;
            entry.name = name;
            entry.root = "profiles/" + id;
            registry.upsert(entry);
        } else {
            const mustela::ProfileEntry* existing = registry.find(e->original_id);
            if (existing) {
                mustela::ProfileEntry updated = *existing;
                updated.name = name;
                registry.upsert(updated);
            }
        }
        mustela::save_profile_registry(path, registry);
        refresh_profiles_list();
        flash_status(*e->b, "profiles updated");
    }
    gtk_widget_destroy(GTK_WIDGET(dialog));
}

void open_profile_editor(Browser& b, const std::string& id, const std::string& name) {
    ProfileEditor* e = new ProfileEditor;
    e->b = &b;
    e->original_id = id;
    e->dialog = gtk_dialog_new_with_buttons(
        id.empty() ? "Add profile — Kiwi" : "Rename profile — Kiwi",
        b.settings_window ? GTK_WINDOW(b.settings_window) : GTK_WINDOW(b.window),
        static_cast<GtkDialogFlags>(GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT),
        "_Cancel", GTK_RESPONSE_CANCEL,
        "_Save", GTK_RESPONSE_OK,
        nullptr);
    GtkWidget* save_btn = gtk_dialog_get_widget_for_response(GTK_DIALOG(e->dialog), GTK_RESPONSE_OK);
    if (save_btn) gtk_style_context_add_class(gtk_widget_get_style_context(save_btn), "kiwi-accent");
    g_signal_connect(e->dialog, "destroy", G_CALLBACK(on_profile_editor_destroy), e);
    g_signal_connect(e->dialog, "response", G_CALLBACK(on_profile_editor_response), e);

    GtkWidget* grid = gtk_grid_new();
    gtk_grid_set_row_spacing(GTK_GRID(grid), 4);
    gtk_grid_set_column_spacing(GTK_GRID(grid), 8);
    gtk_container_set_border_width(GTK_CONTAINER(grid), 8);

    GtkWidget* label = gtk_label_new("Name");
    gtk_label_set_xalign(GTK_LABEL(label), 0.0f);
    gtk_grid_attach(GTK_GRID(grid), label, 0, 0, 1, 1);

    e->name = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(e->name), "Work");
    if (!name.empty()) gtk_entry_set_text(GTK_ENTRY(e->name), name.c_str());
    gtk_grid_attach(GTK_GRID(grid), e->name, 1, 0, 1, 1);

    e->hint = gtk_label_new(id.empty()
        ? "The identifier and data directory are derived from the name."
        : "Only the display name changes; the identifier stays stable.");
    gtk_style_context_add_class(gtk_widget_get_style_context(e->hint), "kiwi-secondary");
    gtk_label_set_xalign(GTK_LABEL(e->hint), 0.0f);

    GtkWidget* content = gtk_dialog_get_content_area(GTK_DIALOG(e->dialog));
    gtk_box_pack_start(GTK_BOX(content), grid, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(content), e->hint, FALSE, FALSE, 4);

    gtk_widget_show_all(e->dialog);
}

void on_profile_add(GtkButton*, gpointer) {
    if (!g_prefs.b) return;
    open_profile_editor(*g_prefs.b, "", "");
}

void on_profile_rename(GtkButton*, gpointer) {
    if (!g_prefs.b) return;
    std::string id = selected_row_text(g_prefs.profiles_tree, PC_ID);
    if (id.empty()) return;
    mustela::ProfileRegistry registry = mustela::load_profile_registry(config_file_path("profiles.toml"));
    const mustela::ProfileEntry* entry = registry.find(id);
    if (entry) open_profile_editor(*g_prefs.b, id, entry->name);
}

void on_profile_row_activated(GtkTreeView*, GtkTreePath*, GtkTreeViewColumn*, gpointer) {
    on_profile_rename(nullptr, nullptr);
}

void on_profile_use(GtkButton*, gpointer) {
    if (!g_prefs.b) return;
    std::string id = selected_row_text(g_prefs.profiles_tree, PC_ID);
    if (id.empty()) return;
    if (id == g_prefs.b->settings.shell_profile) {
        flash_status(*g_prefs.b, "profile is already active");
        return;
    }
    std::string path = config_file_path("profiles.toml");
    mustela::ProfileRegistry registry = mustela::load_profile_registry(path);
    if (!registry.find(id)) return;
    registry.default_id = id;
    mustela::save_profile_registry(path, registry);
    g_prefs.b->settings.shell_profile = id;
    mustela::save_settings(config_file_path("settings.toml"), g_prefs.b->settings);
    refresh_profiles_list();
    flash_status(*g_prefs.b, "profile switch applies after restart");
}

void on_profile_remove(GtkButton*, gpointer) {
    if (!g_prefs.b) return;
    std::string id = selected_row_text(g_prefs.profiles_tree, PC_ID);
    if (id.empty()) return;
    if (id == g_prefs.b->settings.shell_profile) {
        flash_status(*g_prefs.b, "cannot remove the active profile");
        return;
    }
    std::string path = config_file_path("profiles.toml");
    mustela::ProfileRegistry registry = mustela::load_profile_registry(path);
    if (registry.entries.size() <= 1) {
        flash_status(*g_prefs.b, "at least one profile must remain");
        return;
    }
    if (!registry.remove(id)) return;
    mustela::save_profile_registry(path, registry);
    refresh_profiles_list();
    flash_status(*g_prefs.b, "profile removed");
}

// ---- window ----

void on_prefs_destroy(GtkWidget*, gpointer) {
    g_prefs = PrefsPage();
}

void on_prefs_destroy_notify(Browser* b) {
    if (b) b->settings_window = nullptr;
}

GtkWidget* build_engines_page(Browser&) {
    GtkWidget* page = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
    gtk_container_set_border_width(GTK_CONTAINER(page), 6);

    GtkWidget* hint = gtk_label_new(
        "!keyword query in the address bar searches with that engine; plain queries use the default.");
    gtk_style_context_add_class(gtk_widget_get_style_context(hint), "kiwi-secondary");
    gtk_label_set_xalign(GTK_LABEL(hint), 0.0f);
    gtk_box_pack_start(GTK_BOX(page), hint, FALSE, FALSE, 0);

    g_prefs.engines_store = gtk_list_store_new(EC_N, G_TYPE_STRING, G_TYPE_STRING,
        G_TYPE_STRING, G_TYPE_STRING);
    GtkWidget* tree = gtk_tree_view_new_with_model(GTK_TREE_MODEL(g_prefs.engines_store));
    g_object_unref(g_prefs.engines_store);
    g_prefs.engines_tree = tree;

    GtkCellRenderer* renderer = gtk_cell_renderer_text_new();
    const char* titles[] = {"Keyword", "Name", "URL template", ""};
    for (int col = 0; col < EC_N; ++col) {
        gtk_tree_view_insert_column_with_attributes(GTK_TREE_VIEW(tree), -1, titles[col], renderer,
            "text", col, nullptr);
    }
    gtk_tree_view_column_set_expand(gtk_tree_view_get_column(GTK_TREE_VIEW(tree), EC_TEMPLATE), TRUE);
    g_signal_connect(tree, "row-activated", G_CALLBACK(on_engine_row_activated), nullptr);

    GtkWidget* scroll = gtk_scrolled_window_new(nullptr, nullptr);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll), GTK_POLICY_AUTOMATIC,
        GTK_POLICY_AUTOMATIC);
    gtk_container_add(GTK_CONTAINER(scroll), tree);

    GtkWidget* add_btn = gtk_button_new_with_label("Add…");
    gtk_style_context_add_class(gtk_widget_get_style_context(add_btn), "kiwi-accent");
    GtkWidget* edit_btn = gtk_button_new_with_label("Edit…");
    GtkWidget* remove_btn = gtk_button_new_with_label("Remove");
    GtkWidget* default_btn = gtk_button_new_with_label("Set default");
    g_signal_connect(add_btn, "clicked", G_CALLBACK(on_engine_add), nullptr);
    g_signal_connect(edit_btn, "clicked", G_CALLBACK(on_engine_edit), nullptr);
    g_signal_connect(remove_btn, "clicked", G_CALLBACK(on_engine_remove), nullptr);
    g_signal_connect(default_btn, "clicked", G_CALLBACK(on_engine_default), nullptr);

    GtkWidget* buttons = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
    gtk_box_pack_start(GTK_BOX(buttons), add_btn, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(buttons), edit_btn, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(buttons), remove_btn, FALSE, FALSE, 0);
    gtk_box_pack_end(GTK_BOX(buttons), default_btn, FALSE, FALSE, 0);

    gtk_box_pack_start(GTK_BOX(page), scroll, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(page), buttons, FALSE, FALSE, 0);
    return page;
}

GtkWidget* build_profiles_page(Browser&) {
    GtkWidget* page = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
    gtk_container_set_border_width(GTK_CONTAINER(page), 6);

    GtkWidget* hint = gtk_label_new(
        "Profiles isolate history, bookmarks, autofill and stored data. Switching applies after restart.");
    gtk_style_context_add_class(gtk_widget_get_style_context(hint), "kiwi-secondary");
    gtk_label_set_xalign(GTK_LABEL(hint), 0.0f);
    gtk_box_pack_start(GTK_BOX(page), hint, FALSE, FALSE, 0);

    g_prefs.profiles_store = gtk_list_store_new(PC_N, G_TYPE_STRING, G_TYPE_STRING,
        G_TYPE_STRING, G_TYPE_STRING);
    GtkWidget* tree = gtk_tree_view_new_with_model(GTK_TREE_MODEL(g_prefs.profiles_store));
    g_object_unref(g_prefs.profiles_store);
    g_prefs.profiles_tree = tree;

    GtkCellRenderer* renderer = gtk_cell_renderer_text_new();
    const char* titles[] = {"", "Name", "ID", "Data root"};
    for (int col = 0; col < PC_N; ++col) {
        gtk_tree_view_insert_column_with_attributes(GTK_TREE_VIEW(tree), -1, titles[col], renderer,
            "text", col, nullptr);
    }
    gtk_tree_view_column_set_expand(gtk_tree_view_get_column(GTK_TREE_VIEW(tree), PC_NAME), TRUE);
    gtk_tree_view_column_set_expand(gtk_tree_view_get_column(GTK_TREE_VIEW(tree), PC_ROOT), TRUE);
    g_signal_connect(tree, "row-activated", G_CALLBACK(on_profile_row_activated), nullptr);

    GtkWidget* scroll = gtk_scrolled_window_new(nullptr, nullptr);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll), GTK_POLICY_AUTOMATIC,
        GTK_POLICY_AUTOMATIC);
    gtk_container_add(GTK_CONTAINER(scroll), tree);

    GtkWidget* add_btn = gtk_button_new_with_label("Add…");
    gtk_style_context_add_class(gtk_widget_get_style_context(add_btn), "kiwi-accent");
    GtkWidget* rename_btn = gtk_button_new_with_label("Rename…");
    GtkWidget* remove_btn = gtk_button_new_with_label("Remove");
    GtkWidget* use_btn = gtk_button_new_with_label("Use this profile");
    g_signal_connect(add_btn, "clicked", G_CALLBACK(on_profile_add), nullptr);
    g_signal_connect(rename_btn, "clicked", G_CALLBACK(on_profile_rename), nullptr);
    g_signal_connect(remove_btn, "clicked", G_CALLBACK(on_profile_remove), nullptr);
    g_signal_connect(use_btn, "clicked", G_CALLBACK(on_profile_use), nullptr);

    GtkWidget* buttons = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
    gtk_box_pack_start(GTK_BOX(buttons), add_btn, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(buttons), rename_btn, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(buttons), remove_btn, FALSE, FALSE, 0);
    gtk_box_pack_end(GTK_BOX(buttons), use_btn, FALSE, FALSE, 0);

    gtk_box_pack_start(GTK_BOX(page), scroll, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(page), buttons, FALSE, FALSE, 0);
    return page;
}

// ---- updates page (K4: check for updates against GitHub Releases) ----

void on_updates_check_clicked(GtkButton*, gpointer) {
    if (g_prefs.b) kiwi::updates_check(g_prefs.b);
}

void on_updates_download_clicked(GtkButton*, gpointer) {
    if (g_prefs.b) kiwi::updates_download(g_prefs.b);
}

void on_updates_open_clicked(GtkButton*, gpointer) {
    if (g_prefs.b) kiwi::updates_open_folder(g_prefs.b);
}

std::string updates_status_text() {
    const kiwi::UpdateInfo& u = kiwi::updates();
    if (u.busy) {
        return "Downloading update… " + std::to_string(u.percent.load()) + "%";
    }
    if (u.checking) return "Checking GitHub Releases…";
    if (!u.error.empty()) return "Update check failed: " + u.error;
    if (u.available) {
        std::string line = "Version " + u.latest_version + " is available";
        if (!u.asset_name.empty()) line += " — " + u.asset_name;
        if (!u.downloaded_path.empty()) line += "\nSaved to " + u.downloaded_path;
        return line;
    }
    if (u.up_to_date) return "You are up to date (Kiwi " + u.current_version + ")";
    if (!u.downloaded_path.empty()) return "Saved to " + u.downloaded_path;
    return "Kiwi " + u.current_version + " — releases live on GitHub";
}

void refresh_updates_widgets() {
    const kiwi::UpdateInfo& u = kiwi::updates();
    if (g_prefs.updates_status) {
        gtk_label_set_text(GTK_LABEL(g_prefs.updates_status), updates_status_text().c_str());
        gtk_label_set_line_wrap(GTK_LABEL(g_prefs.updates_status), TRUE);
    }
    if (g_prefs.updates_notes) {
        std::string notes = u.notes;
        if (!u.release_url.empty() && notes.size() > 400) notes = notes.substr(0, 400) + "…";
        gtk_label_set_text(GTK_LABEL(g_prefs.updates_notes), notes.c_str());
        gtk_widget_set_visible(g_prefs.updates_notes, !notes.empty());
    }
    if (g_prefs.updates_check_btn) {
        gtk_widget_set_sensitive(g_prefs.updates_check_btn, !u.checking && !u.busy);
    }
    if (g_prefs.updates_download_btn) {
        gtk_widget_set_sensitive(g_prefs.updates_download_btn, u.available && !u.busy);
        gtk_widget_set_visible(g_prefs.updates_download_btn, u.available);
    }
    if (g_prefs.updates_open_btn) {
        gtk_widget_set_sensitive(g_prefs.updates_open_btn, !u.downloaded_path.empty());
    }
}

GtkWidget* build_updates_page(Browser& b) {
    (void)b;
    GtkWidget* page = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
    gtk_container_set_border_width(GTK_CONTAINER(page), 12);

    GtkWidget* title = gtk_label_new(nullptr);
    std::string markup = std::string("<span size=\"x-large\" weight=\"bold\">Kiwi ")
        + KIWI_VERSION + "</span>";
    gtk_label_set_markup(GTK_LABEL(title), markup.c_str());
    gtk_box_pack_start(GTK_BOX(page), title, FALSE, FALSE, 0);

    GtkWidget* subtitle = gtk_label_new("Privacy-first web browser powered by the Mustela engine");
    gtk_style_context_add_class(gtk_widget_get_style_context(subtitle), "kiwi-secondary");
    gtk_box_pack_start(GTK_BOX(page), subtitle, FALSE, FALSE, 0);

    GtkWidget* sep1 = gtk_separator_new(GTK_ORIENTATION_HORIZONTAL);
    gtk_box_pack_start(GTK_BOX(page), sep1, FALSE, FALSE, 4);

    GtkWidget* updates_title = gtk_label_new(nullptr);
    gtk_label_set_markup(GTK_LABEL(updates_title),
        "<span weight=\"bold\">Updates</span>");
    gtk_label_set_xalign(GTK_LABEL(updates_title), 0.0f);
    gtk_box_pack_start(GTK_BOX(page), updates_title, FALSE, FALSE, 0);

    GtkWidget* status = gtk_label_new("");
    gtk_label_set_line_wrap(GTK_LABEL(status), TRUE);
    gtk_label_set_xalign(GTK_LABEL(status), 0.0f);
    gtk_label_set_selectable(GTK_LABEL(status), TRUE);
    g_prefs.updates_status = status;
    gtk_box_pack_start(GTK_BOX(page), status, FALSE, FALSE, 0);

    GtkWidget* notes = gtk_label_new("");
    gtk_label_set_line_wrap(GTK_LABEL(notes), TRUE);
    gtk_label_set_xalign(GTK_LABEL(notes), 0.0f);
    gtk_label_set_selectable(GTK_LABEL(notes), TRUE);
    gtk_label_set_max_width_chars(GTK_LABEL(notes), 72);
    gtk_widget_set_no_show_all(notes, TRUE);
    g_prefs.updates_notes = notes;
    gtk_box_pack_start(GTK_BOX(page), notes, FALSE, FALSE, 0);

    GtkWidget* buttons = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
    GtkWidget* check_btn = gtk_button_new_with_label("Check for updates");
    gtk_style_context_add_class(gtk_widget_get_style_context(check_btn), "kiwi-accent");
    GtkWidget* download_btn = gtk_button_new_with_label("Download update");
    GtkWidget* open_btn = gtk_button_new_with_label("Show in folder");
    g_signal_connect(check_btn, "clicked", G_CALLBACK(on_updates_check_clicked), nullptr);
    g_signal_connect(download_btn, "clicked", G_CALLBACK(on_updates_download_clicked), nullptr);
    g_signal_connect(open_btn, "clicked", G_CALLBACK(on_updates_open_clicked), nullptr);
    gtk_box_pack_start(GTK_BOX(buttons), check_btn, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(buttons), download_btn, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(buttons), open_btn, FALSE, FALSE, 0);
    g_prefs.updates_check_btn = check_btn;
    g_prefs.updates_download_btn = download_btn;
    g_prefs.updates_open_btn = open_btn;
    gtk_box_pack_start(GTK_BOX(page), buttons, FALSE, FALSE, 0);

    GtkWidget* hint = gtk_label_new(
        "Checks happen only when you press the button — Kiwi never calls home in the background. "
        "Update packages are downloaded from the Kiwi GitHub Releases page.");
    gtk_style_context_add_class(gtk_widget_get_style_context(hint), "kiwi-secondary");
    gtk_label_set_line_wrap(GTK_LABEL(hint), TRUE);
    gtk_label_set_xalign(GTK_LABEL(hint), 0.0f);
    gtk_box_pack_start(GTK_BOX(page), hint, FALSE, FALSE, 4);

    refresh_updates_widgets();
    return page;
}

}

namespace kiwi {

bool updates_ui_bound() {
    return g_prefs.b != nullptr && g_prefs.updates_status != nullptr;
}

void updates_refresh_ui(Browser* b) {
    (void)b;
    if (g_prefs.b && g_prefs.updates_status) refresh_updates_widgets();
}

}

// ---- import from browsers (K2 leftover) ----

namespace {

void refresh_import_list() {
    if (!g_prefs.import_store) return;
    gtk_list_store_clear(g_prefs.import_store);
    std::vector<mustela::DetectedProfile> found = mustela::detect_browsers();
    for (const mustela::DetectedProfile& item : found) {
        GtkTreeIter iter;
        gtk_list_store_append(g_prefs.import_store, &iter);
        gtk_list_store_set(g_prefs.import_store, &iter,
            IC_BROWSER, item.browser.c_str(),
            IC_PROFILE, item.profile.c_str(),
            IC_PATH, item.path.c_str(), -1);
    }
    if (found.empty() && g_prefs.import_status) {
        gtk_label_set_text(GTK_LABEL(g_prefs.import_status),
            "No other browser profiles detected.");
    }
}

void set_import_status(const std::string& text) {
    if (g_prefs.import_status) {
        gtk_label_set_text(GTK_LABEL(g_prefs.import_status), text.c_str());
    }
}

void apply_import_report(const mustela::ImportReport& report) {
    if (!report.error.empty()) {
        set_import_status("Import failed: " + report.error);
        return;
    }
    std::string text = "Imported " + std::to_string(report.bookmarks_added)
        + " bookmark" + (report.bookmarks_added == 1 ? "" : "s");
    if (report.bookmarks_skipped > 0) {
        text += " (" + std::to_string(report.bookmarks_skipped) + " duplicate"
            + (report.bookmarks_skipped == 1 ? "" : "s") + " skipped)";
    }
    set_import_status(text);
    if (g_prefs.b) {
        refresh_bookmarks_window(*g_prefs.b);
        flash_status(*g_prefs.b, text);
    }
}

void on_import_rescan(GtkButton*, gpointer) {
    refresh_import_list();
    set_import_status("Rescanned.");
}

void on_import_profile(GtkButton*, gpointer) {
    if (!g_prefs.b) return;
    std::string path = selected_row_text(g_prefs.import_tree, IC_PATH);
    std::string browser = selected_row_text(g_prefs.import_tree, IC_BROWSER);
    if (path.empty()) {
        set_import_status("Select a detected browser profile first.");
        return;
    }
    std::string lower = to_lower_ascii(browser);
    if (lower == "firefox") {
        apply_import_report(mustela::import_bookmarks_firefox(path, *g_prefs.b->bookmarks));
    } else {
        apply_import_report(mustela::import_bookmarks_chromium(path, *g_prefs.b->bookmarks));
    }
}

void on_import_html(GtkButton*, gpointer) {
    if (!g_prefs.b) return;
    GtkWidget* chooser = gtk_file_chooser_dialog_new("Import bookmarks from HTML — Kiwi",
        GTK_WINDOW(g_prefs.b->settings_window ? g_prefs.b->settings_window : g_prefs.b->window),
        GTK_FILE_CHOOSER_ACTION_OPEN,
        "Cancel", GTK_RESPONSE_CANCEL, "Import", GTK_RESPONSE_ACCEPT, nullptr);
    GtkFileFilter* filter = gtk_file_filter_new();
    gtk_file_filter_set_name(filter, "Bookmarks HTML");
    gtk_file_filter_add_pattern(filter, "*.html");
    gtk_file_filter_add_pattern(filter, "*.htm");
    gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(chooser), filter);
    gint choice = gtk_dialog_run(GTK_DIALOG(chooser));
    std::string path;
    if (choice == GTK_RESPONSE_ACCEPT) {
        gchar* chosen = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(chooser));
        if (chosen) path = chosen;
        g_free(chosen);
    }
    gtk_widget_destroy(chooser);
    if (path.empty()) return;
    apply_import_report(mustela::import_bookmarks_html(path, *g_prefs.b->bookmarks));
}

GtkWidget* build_import_page(Browser& b) {
    (void)b;
    GtkWidget* page = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
    gtk_container_set_border_width(GTK_CONTAINER(page), 6);

    GtkWidget* hint = gtk_label_new(
        "Imports bookmarks from browsers installed on this machine. "
        "History and passwords are not touched.");
    gtk_style_context_add_class(gtk_widget_get_style_context(hint), "kiwi-secondary");
    gtk_label_set_xalign(GTK_LABEL(hint), 0.0f);
    gtk_label_set_line_wrap(GTK_LABEL(hint), TRUE);
    gtk_box_pack_start(GTK_BOX(page), hint, FALSE, FALSE, 0);

    g_prefs.import_store = gtk_list_store_new(IC_N, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING);
    GtkWidget* tree = gtk_tree_view_new_with_model(GTK_TREE_MODEL(g_prefs.import_store));
    g_object_unref(g_prefs.import_store);
    g_prefs.import_tree = tree;

    GtkCellRenderer* renderer = gtk_cell_renderer_text_new();
    const char* titles[] = {"Browser", "Profile", "Location"};
    for (int col = 0; col < IC_N; ++col) {
        gtk_tree_view_insert_column_with_attributes(GTK_TREE_VIEW(tree), -1, titles[col], renderer,
            "text", col, nullptr);
    }
    gtk_tree_view_column_set_expand(gtk_tree_view_get_column(GTK_TREE_VIEW(tree), IC_PATH), TRUE);

    GtkWidget* scroll = gtk_scrolled_window_new(nullptr, nullptr);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll), GTK_POLICY_AUTOMATIC,
        GTK_POLICY_AUTOMATIC);
    gtk_widget_set_size_request(scroll, -1, 180);
    gtk_container_add(GTK_CONTAINER(scroll), tree);

    GtkWidget* rescan_btn = gtk_button_new_with_label("Rescan");
    GtkWidget* import_btn = gtk_button_new_with_label("Import bookmarks");
    gtk_style_context_add_class(gtk_widget_get_style_context(import_btn), "kiwi-accent");
    g_signal_connect(rescan_btn, "clicked", G_CALLBACK(on_import_rescan), nullptr);
    g_signal_connect(import_btn, "clicked", G_CALLBACK(on_import_profile), nullptr);

    GtkWidget* buttons = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
    gtk_box_pack_start(GTK_BOX(buttons), rescan_btn, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(buttons), import_btn, FALSE, FALSE, 0);

    GtkWidget* status = gtk_label_new("");
    gtk_label_set_line_wrap(GTK_LABEL(status), TRUE);
    gtk_label_set_xalign(GTK_LABEL(status), 0.0f);
    gtk_label_set_selectable(GTK_LABEL(status), TRUE);
    g_prefs.import_status = status;

    GtkWidget* html_btn = gtk_button_new_with_label("Import from HTML file…");
    GtkWidget* html_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
    GtkWidget* html_hint = gtk_label_new(
        "Any browser can export bookmarks as HTML — Chrome, Edge and Firefox all do.");
    gtk_style_context_add_class(gtk_widget_get_style_context(html_hint), "kiwi-secondary");
    gtk_label_set_xalign(GTK_LABEL(html_hint), 0.0f);
    gtk_box_pack_start(GTK_BOX(html_row), html_btn, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(html_row), html_hint, FALSE, FALSE, 4);

    gtk_box_pack_start(GTK_BOX(page), scroll, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(page), buttons, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(page), status, FALSE, FALSE, 2);
    gtk_box_pack_start(GTK_BOX(page), gtk_separator_new(GTK_ORIENTATION_HORIZONTAL), FALSE, FALSE, 4);
    gtk_box_pack_start(GTK_BOX(page), html_row, FALSE, FALSE, 0);

    refresh_import_list();
    return page;
}

}

void open_settings_window(Browser& b) {
    if (b.settings_window) {
        gtk_window_present(GTK_WINDOW(b.settings_window));
        refresh_engines_list();
        refresh_profiles_list();
        refresh_import_list();
        return;
    }

    GtkWidget* window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    b.settings_window = window;
    g_prefs.b = &b;
    gtk_window_set_title(GTK_WINDOW(window), "Settings — Kiwi");
    gtk_window_set_default_size(GTK_WINDOW(window), 780, 560);
    gtk_window_set_transient_for(GTK_WINDOW(window), GTK_WINDOW(b.window));
    g_signal_connect(window, "destroy", G_CALLBACK(on_prefs_destroy), nullptr);
    g_signal_connect_swapped(window, "destroy", G_CALLBACK(on_prefs_destroy_notify), &b);

    GtkWidget* notebook = gtk_notebook_new();
    gtk_notebook_append_page(GTK_NOTEBOOK(notebook), build_engines_page(b),
        gtk_label_new("Search engines"));
    gtk_notebook_append_page(GTK_NOTEBOOK(notebook), build_profiles_page(b),
        gtk_label_new("Profiles"));
    gtk_notebook_append_page(GTK_NOTEBOOK(notebook), build_import_page(b),
        gtk_label_new("Import"));
    gtk_notebook_append_page(GTK_NOTEBOOK(notebook), build_updates_page(b),
        gtk_label_new("About & updates"));

    gtk_container_add(GTK_CONTAINER(window), notebook);
    gtk_widget_show_all(window);
    refresh_engines_list();
    refresh_profiles_list();
    gtk_notebook_set_current_page(GTK_NOTEBOOK(notebook), 0);
}
