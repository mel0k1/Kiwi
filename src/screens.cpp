#include "browser.hpp"

#include <glib/gstdio.h>

#include <cctype>
#include <cstdint>
#include <cstdio>
#include <ctime>
#include <string>

#include "mustela/mustela.hpp"

namespace {

enum HistoryCols {
    COL_TIME,
    COL_TITLE,
    COL_URL
};

enum DownloadCols {
    DL_NAME,
    DL_PROGRESS,
    DL_PATH
};

std::string format_time(std::int64_t seconds) {
    GDateTime* dt = g_date_time_new_from_unix_local(seconds);
    if (!dt) return "";
    gchar* text = g_date_time_format(dt, "%Y-%m-%d %H:%M");
    std::string out = text ? text : "";
    g_date_time_unref(dt);
    g_free(text);
    return out;
}

void on_history_destroy(GtkWidget*, gpointer user_data) {
    Browser* b = static_cast<Browser*>(user_data);
    if (b) b->history_window = nullptr;
}

void on_history_row_activated(GtkTreeView* tree, GtkTreePath* path, GtkTreeViewColumn*,
    gpointer user_data) {
    Browser* b = static_cast<Browser*>(user_data);
    GtkTreeModel* model = gtk_tree_view_get_model(tree);
    GtkTreeIter iter;
    if (!gtk_tree_model_get_iter(model, &iter, path)) return;
    gchar* url = nullptr;
    gtk_tree_model_get(model, &iter, COL_URL, &url, -1);
    if (url && *url) open_tab(*b, url, false);
    g_free(url);
}

void on_history_clear(GtkButton*, gpointer user_data) {
    Browser* b = static_cast<Browser*>(user_data);
    if (b->history) b->history->clear();
    if (b->history_window) {
        GtkWidget* window = b->history_window;
        b->history_window = nullptr;
        gtk_widget_destroy(window);
    }
}

void on_downloads_destroy(GtkWidget*, gpointer user_data) {
    Browser* b = static_cast<Browser*>(user_data);
    if (!b) return;
    b->downloads_window = nullptr;
    b->downloads_store = nullptr;
    b->downloads_tree = nullptr;
}

}

void open_history_window(Browser& b) {
    if (b.history_window) {
        gtk_window_present(GTK_WINDOW(b.history_window));
        return;
    }

    GtkWidget* window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    b.history_window = window;
    gtk_window_set_title(GTK_WINDOW(window), "History — Kiwi");
    gtk_window_set_default_size(GTK_WINDOW(window), 720, 480);
    gtk_window_set_transient_for(GTK_WINDOW(window), GTK_WINDOW(b.window));
    g_signal_connect(window, "destroy", G_CALLBACK(on_history_destroy), &b);

    GtkListStore* store = gtk_list_store_new(3, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING);
    if (b.history) {
        for (const mustela::HistoryEntry& entry : b.history->recent(500)) {
            GtkTreeIter iter;
            gtk_list_store_append(store, &iter);
            gtk_list_store_set(store, &iter,
                COL_TIME, format_time(entry.visited_at).c_str(),
                COL_TITLE, entry.title.c_str(),
                COL_URL, entry.url.c_str(), -1);
        }
    }

    GtkWidget* tree = gtk_tree_view_new_with_model(GTK_TREE_MODEL(store));
    g_object_unref(store);
    gtk_tree_view_set_headers_visible(GTK_TREE_VIEW(tree), TRUE);

    GtkCellRenderer* renderer = gtk_cell_renderer_text_new();
    gtk_tree_view_insert_column_with_attributes(GTK_TREE_VIEW(tree), -1, "When", renderer,
        "text", COL_TIME, nullptr);
    gtk_tree_view_insert_column_with_attributes(GTK_TREE_VIEW(tree), -1, "Title", renderer,
        "text", COL_TITLE, nullptr);
    gtk_tree_view_insert_column_with_attributes(GTK_TREE_VIEW(tree), -1, "URL", renderer,
        "text", COL_URL, nullptr);
    gtk_tree_view_column_set_expand(gtk_tree_view_get_column(GTK_TREE_VIEW(tree), COL_TITLE), TRUE);
    g_signal_connect(tree, "row-activated", G_CALLBACK(on_history_row_activated), &b);

    GtkWidget* scroll = gtk_scrolled_window_new(nullptr, nullptr);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll), GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
    gtk_container_add(GTK_CONTAINER(scroll), tree);

    GtkWidget* clear_btn = gtk_button_new_with_label("Clear history");
    g_signal_connect(clear_btn, "clicked", G_CALLBACK(on_history_clear), &b);

    GtkWidget* bottom = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
    gtk_box_pack_end(GTK_BOX(bottom), clear_btn, FALSE, FALSE, 0);

    GtkWidget* layout = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
    gtk_container_set_border_width(GTK_CONTAINER(layout), 4);
    gtk_box_pack_start(GTK_BOX(layout), scroll, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(layout), bottom, FALSE, FALSE, 0);
    gtk_container_add(GTK_CONTAINER(window), layout);

    gtk_widget_show_all(window);
}

void refresh_downloads_window(Browser& b) {
    if (!b.downloads_window || !b.downloads_store) return;
    gtk_list_store_clear(b.downloads_store);
    for (const auto& pair : b.downloads) {
        const DownloadRow& row = pair.second;
        std::string progress;
        if (row.failed) progress = "failed";
        else if (!row.running) progress = "done";
        else progress = std::to_string(static_cast<int>(row.progress * 100.0)) + "%";
        GtkTreeIter iter;
        gtk_list_store_append(b.downloads_store, &iter);
        gtk_list_store_set(b.downloads_store, &iter,
            DL_NAME, row.name.c_str(),
            DL_PROGRESS, progress.c_str(),
            DL_PATH, row.path.c_str(), -1);
    }
}

void open_downloads_window(Browser& b) {
    if (b.downloads_window) {
        gtk_window_present(GTK_WINDOW(b.downloads_window));
        refresh_downloads_window(b);
        return;
    }

    GtkWidget* window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    b.downloads_window = window;
    gtk_window_set_title(GTK_WINDOW(window), "Downloads — Kiwi");
    gtk_window_set_default_size(GTK_WINDOW(window), 640, 400);
    gtk_window_set_transient_for(GTK_WINDOW(window), GTK_WINDOW(b.window));
    g_signal_connect(window, "destroy", G_CALLBACK(on_downloads_destroy), &b);

    b.downloads_store = gtk_list_store_new(3, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING);
    GtkWidget* tree = gtk_tree_view_new_with_model(GTK_TREE_MODEL(b.downloads_store));
    g_object_ref(b.downloads_store);
    b.downloads_tree = tree;
    gtk_tree_view_set_headers_visible(GTK_TREE_VIEW(tree), TRUE);

    GtkCellRenderer* renderer = gtk_cell_renderer_text_new();
    gtk_tree_view_insert_column_with_attributes(GTK_TREE_VIEW(tree), -1, "File", renderer,
        "text", DL_NAME, nullptr);
    gtk_tree_view_insert_column_with_attributes(GTK_TREE_VIEW(tree), -1, "Status", renderer,
        "text", DL_PROGRESS, nullptr);
    gtk_tree_view_insert_column_with_attributes(GTK_TREE_VIEW(tree), -1, "Saved to", renderer,
        "text", DL_PATH, nullptr);
    gtk_tree_view_column_set_expand(gtk_tree_view_get_column(GTK_TREE_VIEW(tree), DL_PATH), TRUE);

    GtkWidget* scroll = gtk_scrolled_window_new(nullptr, nullptr);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll), GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
    gtk_container_add(GTK_CONTAINER(scroll), tree);

    GtkWidget* layout = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
    gtk_container_set_border_width(GTK_CONTAINER(layout), 4);
    gtk_box_pack_start(GTK_BOX(layout), scroll, TRUE, TRUE, 0);
    gtk_container_add(GTK_CONTAINER(window), layout);

    gtk_widget_show_all(window);
    refresh_downloads_window(b);
}

// ---------------- Privacy Center (K2) ----------------

#include <algorithm>
#include <cctype>
#include <functional>
#include <vector>

enum SiteCols {
    SC_HOST,
    SC_ZOOM,
    SC_JS,
    SC_COOKIES,
    SC_FP,
    SC_PERM,
    SC_UA,
    SC_CSS,
    SC_N
};

enum DataCols { DC_ORIGIN, DC_KINDS, DC_N };

struct PrivacyPage {
    Browser* b = nullptr;
    GtkListStore* sites_store = nullptr;
    GtkListStore* data_store = nullptr;
    GtkWidget* data_status = nullptr;
    GtkWidget* clear_btn = nullptr;
    GtkWidget* remove_btn = nullptr;
    GtkWidget* scan_btn = nullptr;
    GtkWidget* kind_cookies = nullptr;
    GtkWidget* kind_cache = nullptr;
    GtkWidget* kind_local = nullptr;
    GtkWidget* kind_idb = nullptr;
    GtkWidget* age_combo = nullptr;
    std::string focus_host;
};

PrivacyPage g_privacy;

std::string trim_copy(const std::string& in) {
    size_t start = 0;
    size_t end = in.size();
    while (start < end && std::isspace(static_cast<unsigned char>(in[start]))) ++start;
    while (end > start && std::isspace(static_cast<unsigned char>(in[end - 1]))) --end;
    return in.substr(start, end - start);
}

const char* rule_cell(mustela::SiteRule rule) {
    switch (rule) {
        case mustela::SiteRule::Allow: return "allow";
        case mustela::SiteRule::Deny: return "deny";
        case mustela::SiteRule::Default: break;
    }
    return "\xE2\x80\x94";
}

std::string zoom_cell(double zoom) {
    if (zoom == 0.0) return "\xE2\x80\x94";
    char buffer[32];
    std::snprintf(buffer, sizeof(buffer), "%.2fx", zoom);
    return buffer;
}

std::string kinds_text(mustela::DataKind kinds) {
    std::string out;
    const auto add = [&out](const char* text) {
        if (!out.empty()) out += ", ";
        out += text;
    };
    if (mustela::has(kinds, mustela::DataKind::Cookies)) add("cookies");
    if (mustela::has(kinds, mustela::DataKind::Cache)) add("cache");
    if (mustela::has(kinds, mustela::DataKind::LocalStorage)) add("local storage");
    if (mustela::has(kinds, mustela::DataKind::IndexedDB)) add("indexeddb");
    if (out.empty()) out = "\xE2\x80\x94";
    return out;
}

void update_privacy_indicator(Browser& b) {
    if (!b.privacy_btn) return;
    mustela::SitePolicy policy;
    std::string host;
    KiwiTab* tab = active_tab(b);
    if (tab && tab->view) {
        policy = tab->view->active_site_policy();
        host = mustela::host_from_uri(tab->uri);
    }
    std::vector<std::string> marks;
    if (policy.zoom != 0.0) marks.push_back("zoom");
    if (!policy.user_agent.empty()) marks.push_back("user agent");
    if (!policy.css.empty()) marks.push_back("custom css");
    if (policy.javascript != mustela::SiteRule::Default)
        marks.push_back(std::string("js: ") + mustela::site_rule_name(policy.javascript));
    if (policy.cookies != mustela::SiteRule::Default)
        marks.push_back(std::string("cookies: ") + mustela::site_rule_name(policy.cookies));
    if (policy.fingerprint != mustela::SiteRule::Default)
        marks.push_back(std::string("fingerprint: ") + mustela::site_rule_name(policy.fingerprint));
    if (policy.permissions != mustela::SiteRule::Default)
        marks.push_back(std::string("permissions: ") + mustela::site_rule_name(policy.permissions));

    GtkWidget* dot = gtk_bin_get_child(GTK_BIN(b.privacy_btn));
    GtkStyleContext* ctx = gtk_widget_get_style_context(dot);
    if (!marks.empty()) {
        gtk_style_context_remove_class(ctx, "kiwi-dot-idle");
        gtk_style_context_add_class(ctx, "kiwi-dot-active");
        std::string tip = "Site overrides on " + (host.empty() ? std::string("this site") : host) + ":";
        for (size_t i = 0; i < marks.size(); ++i) {
            tip += (i ? ", " : " ");
            tip += marks[i];
        }
        gtk_widget_set_tooltip_text(b.privacy_btn, tip.c_str());
    } else {
        gtk_style_context_remove_class(ctx, "kiwi-dot-active");
        gtk_style_context_add_class(ctx, "kiwi-dot-idle");
        gtk_widget_set_tooltip_text(b.privacy_btn,
            "Privacy Center — no overrides for this site");
    }
}

void apply_site_policy_changes(Browser& b) {
    for (std::unique_ptr<KiwiTab>& tab : b.tabs) {
        if (tab && tab->view) tab->view->refresh_policy();
    }
    update_privacy_indicator(b);
}

void refresh_site_list() {
    if (!g_privacy.sites_store) return;
    gtk_list_store_clear(g_privacy.sites_store);
    if (!g_privacy.b || !g_privacy.b->sites) return;
    for (const mustela::SiteRules::Entry& entry : g_privacy.b->sites->entries()) {
        const mustela::SitePolicy& p = entry.policy;
        GtkTreeIter iter;
        gtk_list_store_append(g_privacy.sites_store, &iter);
        gtk_list_store_set(g_privacy.sites_store, &iter,
            SC_HOST, entry.host.c_str(),
            SC_ZOOM, zoom_cell(p.zoom).c_str(),
            SC_JS, rule_cell(p.javascript),
            SC_COOKIES, rule_cell(p.cookies),
            SC_FP, rule_cell(p.fingerprint),
            SC_PERM, rule_cell(p.permissions),
            SC_UA, p.user_agent.empty() ? "\xE2\x80\x94" : "yes",
            SC_CSS, p.css.empty() ? "\xE2\x80\x94" : "yes", -1);
    }
}

// ---- site override editor ----

struct SiteEditor {
    Browser* b = nullptr;
    GtkWidget* dialog = nullptr;
    GtkWidget* host = nullptr;
    GtkWidget* js = nullptr;
    GtkWidget* cookies = nullptr;
    GtkWidget* fingerprint = nullptr;
    GtkWidget* permissions = nullptr;
    GtkWidget* zoom = nullptr;
    GtkWidget* ua = nullptr;
    GtkWidget* css = nullptr;
    std::string original_host;
};

void rule_combo_set(GtkWidget* combo, mustela::SiteRule rule) {
    int index = 0;
    if (rule == mustela::SiteRule::Allow) index = 1;
    if (rule == mustela::SiteRule::Deny) index = 2;
    gtk_combo_box_set_active(GTK_COMBO_BOX(combo), index);
}

mustela::SiteRule rule_combo_get(GtkWidget* combo) {
    switch (gtk_combo_box_get_active(GTK_COMBO_BOX(combo))) {
        case 1: return mustela::SiteRule::Allow;
        case 2: return mustela::SiteRule::Deny;
        default: return mustela::SiteRule::Default;
    }
}

GtkWidget* rule_combo_new() {
    GtkWidget* combo = gtk_combo_box_text_new();
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(combo), "default (engine setting)");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(combo), "allow");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(combo), "deny");
    return combo;
}

void on_editor_destroy(GtkWidget*, gpointer user_data) {
    delete static_cast<SiteEditor*>(user_data);
}

void on_editor_response(GtkDialog* dialog, gint response, gpointer user_data) {
    SiteEditor* e = static_cast<SiteEditor*>(user_data);
    if (response == GTK_RESPONSE_OK && e && e->b && e->b->sites) {
        std::string host = trim_copy(gtk_entry_get_text(GTK_ENTRY(e->host)));
        mustela::SitePolicy p;
        p.javascript = rule_combo_get(e->js);
        p.cookies = rule_combo_get(e->cookies);
        p.fingerprint = rule_combo_get(e->fingerprint);
        p.permissions = rule_combo_get(e->permissions);
        p.zoom = gtk_spin_button_get_value(GTK_SPIN_BUTTON(e->zoom));
        p.user_agent = trim_copy(gtk_entry_get_text(GTK_ENTRY(e->ua)));
        p.css = trim_copy(gtk_entry_get_text(GTK_ENTRY(e->css)));

        bool empty = p.zoom == 0.0 && p.user_agent.empty() && p.css.empty()
            && p.javascript == mustela::SiteRule::Default
            && p.cookies == mustela::SiteRule::Default
            && p.fingerprint == mustela::SiteRule::Default
            && p.permissions == mustela::SiteRule::Default;

        if (!e->original_host.empty() && e->original_host != host) e->b->sites->remove(e->original_host);
        if (host.empty() || empty) {
            e->b->sites->remove(host);
        } else {
            e->b->sites->set(host, p);
        }
        e->b->sites->save();
        apply_site_policy_changes(*e->b);
        refresh_site_list();
    }
    gtk_widget_destroy(GTK_WIDGET(dialog));
}

void open_site_editor(Browser& b, const std::string& host, const mustela::SitePolicy* existing) {
    SiteEditor* e = new SiteEditor;
    e->b = &b;
    e->original_host = host;
    e->dialog = gtk_dialog_new_with_buttons(
        host.empty() ? "Add site overrides — Kiwi" : "Edit site overrides — Kiwi",
        b.privacy_window ? GTK_WINDOW(b.privacy_window) : GTK_WINDOW(b.window),
        static_cast<GtkDialogFlags>(GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT),
        "_Cancel", GTK_RESPONSE_CANCEL,
        "_Save", GTK_RESPONSE_OK,
        nullptr);
    gtk_window_set_default_size(GTK_WINDOW(e->dialog), 480, -1);
    GtkWidget* save_btn = gtk_dialog_get_widget_for_response(GTK_DIALOG(e->dialog), GTK_RESPONSE_OK);
    if (save_btn) gtk_style_context_add_class(gtk_widget_get_style_context(save_btn), "kiwi-accent");
    g_signal_connect(e->dialog, "destroy", G_CALLBACK(on_editor_destroy), e);
    g_signal_connect(e->dialog, "response", G_CALLBACK(on_editor_response), e);

    GtkWidget* grid = gtk_grid_new();
    gtk_grid_set_row_spacing(GTK_GRID(grid), 4);
    gtk_grid_set_column_spacing(GTK_GRID(grid), 8);
    gtk_container_set_border_width(GTK_CONTAINER(grid), 8);

    const auto attach_row = [&grid](int row, const char* title, GtkWidget* widget) {
        GtkWidget* label = gtk_label_new(title);
        gtk_label_set_xalign(GTK_LABEL(label), 0.0f);
        gtk_grid_attach(GTK_GRID(grid), label, 0, row, 1, 1);
        gtk_grid_attach(GTK_GRID(grid), widget, 1, row, 1, 1);
    };

    e->host = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(e->host), "example.org");
    if (!host.empty()) gtk_entry_set_text(GTK_ENTRY(e->host), host.c_str());
    attach_row(0, "Host", e->host);

    mustela::SitePolicy defaults;
    e->js = rule_combo_new();
    rule_combo_set(e->js, existing ? existing->javascript : defaults.javascript);
    attach_row(1, "JavaScript", e->js);

    e->cookies = rule_combo_new();
    rule_combo_set(e->cookies, existing ? existing->cookies : defaults.cookies);
    attach_row(2, "Cookies", e->cookies);

    e->fingerprint = rule_combo_new();
    rule_combo_set(e->fingerprint, existing ? existing->fingerprint : defaults.fingerprint);
    attach_row(3, "Fingerprint", e->fingerprint);

    e->permissions = rule_combo_new();
    rule_combo_set(e->permissions, existing ? existing->permissions : defaults.permissions);
    attach_row(4, "Permissions", e->permissions);

    e->zoom = gtk_spin_button_new_with_range(0.0, 5.0, 0.1);
    gtk_spin_button_set_digits(GTK_SPIN_BUTTON(e->zoom), 2);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(e->zoom), existing ? existing->zoom : 0.0);
    attach_row(6, "Zoom (0 = inherit)", e->zoom);

    e->ua = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(e->ua), "custom user agent (empty = inherit)");
    if (existing) gtk_entry_set_text(GTK_ENTRY(e->ua), existing->user_agent.c_str());
    attach_row(7, "User agent", e->ua);

    e->css = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(e->css), "custom css (empty = none)");
    if (existing) gtk_entry_set_text(GTK_ENTRY(e->css), existing->css.c_str());
    attach_row(8, "Style sheet", e->css);

    GtkWidget* content = gtk_dialog_get_content_area(GTK_DIALOG(e->dialog));
    gtk_box_pack_start(GTK_BOX(content), grid, TRUE, TRUE, 0);

    GtkWidget* hint = gtk_label_new(
        "An all-default entry is removed on save. Rules apply on next page load.");
    gtk_style_context_add_class(gtk_widget_get_style_context(hint), "kiwi-secondary");
    gtk_box_pack_start(GTK_BOX(content), hint, FALSE, FALSE, 4);

    gtk_widget_show_all(e->dialog);
}

// ---- site list actions ----

std::string selected_site_host() {
    if (!g_privacy.sites_store) return std::string();
    GtkTreeSelection* selection = g_privacy.b && g_privacy.b->privacy_window
        ? gtk_tree_view_get_selection(GTK_TREE_VIEW(g_object_get_data(
              G_OBJECT(g_privacy.b->privacy_window), "kiwi-site-tree")))
        : nullptr;
    if (!selection) return std::string();
    GtkTreeModel* model = nullptr;
    GtkTreeIter iter;
    if (!gtk_tree_selection_get_selected(selection, &model, &iter)) return std::string();
    gchar* host = nullptr;
    gtk_tree_model_get(model, &iter, SC_HOST, &host, -1);
    std::string out = host ? host : "";
    g_free(host);
    return out;
}

void on_site_add(GtkButton*, gpointer) {
    if (!g_privacy.b) return;
    open_site_editor(*g_privacy.b, g_privacy.focus_host, nullptr);
}

void on_site_edit(GtkButton*, gpointer) {
    if (!g_privacy.b || !g_privacy.b->sites) return;
    std::string host = selected_site_host();
    if (host.empty()) return;
    const mustela::SitePolicy* existing = g_privacy.b->sites->find(host);
    open_site_editor(*g_privacy.b, host, existing ? existing : nullptr);
}

void on_site_row_activated(GtkTreeView*, GtkTreePath*, GtkTreeViewColumn*, gpointer) {
    on_site_edit(nullptr, nullptr);
}

void on_site_remove(GtkButton*, gpointer) {
    if (!g_privacy.b || !g_privacy.b->sites) return;
    std::string host = selected_site_host();
    if (host.empty()) return;
    g_privacy.b->sites->remove(host);
    g_privacy.b->sites->save();
    apply_site_policy_changes(*g_privacy.b);
    refresh_site_list();
}

// ---- stored data actions ----

mustela::DataKind selected_kinds() {
    std::uint32_t kinds = 0;
    if (g_privacy.kind_cookies && gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(g_privacy.kind_cookies)))
        kinds |= static_cast<std::uint32_t>(mustela::DataKind::Cookies);
    if (g_privacy.kind_cache && gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(g_privacy.kind_cache)))
        kinds |= static_cast<std::uint32_t>(mustela::DataKind::Cache);
    if (g_privacy.kind_local && gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(g_privacy.kind_local)))
        kinds |= static_cast<std::uint32_t>(mustela::DataKind::LocalStorage);
    if (g_privacy.kind_idb && gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(g_privacy.kind_idb)))
        kinds |= static_cast<std::uint32_t>(mustela::DataKind::IndexedDB);
    return static_cast<mustela::DataKind>(kinds);
}

std::int64_t selected_age_seconds() {
    switch (g_privacy.age_combo ? gtk_combo_box_get_active(GTK_COMBO_BOX(g_privacy.age_combo)) : 0) {
        case 1: return 24 * 3600;
        case 2: return 3600;
        default: return 0;
    }
}

void set_data_status(const char* text) {
    if (g_privacy.data_status) gtk_label_set_text(GTK_LABEL(g_privacy.data_status), text);
}

void on_clear_clicked(GtkButton*, gpointer) {
    if (!g_privacy.b || !g_privacy.b->session) return;
    mustela::DataKind kinds = selected_kinds();
    if (kinds == mustela::DataKind::None) {
        set_data_status("Select at least one data kind");
        return;
    }
    gtk_widget_set_sensitive(g_privacy.clear_btn, FALSE);
    set_data_status("Clearing…");
    g_privacy.b->session->clear_website_data(kinds, selected_age_seconds(), [] {
        if (!g_privacy.b) return;
        gtk_widget_set_sensitive(g_privacy.clear_btn, TRUE);
        set_data_status("Cleared. Rescan to refresh the list.");
    });
}

void on_scan_clicked(GtkButton*, gpointer) {
    if (!g_privacy.b || !g_privacy.b->session) return;
    gtk_widget_set_sensitive(g_privacy.scan_btn, FALSE);
    if (g_privacy.data_store) gtk_list_store_clear(g_privacy.data_store);
    set_data_status("Scanning…");
    g_privacy.b->session->fetch_website_data([](std::vector<mustela::WebsiteDataEntry> entries) {
        if (!g_privacy.b) return;
        gtk_widget_set_sensitive(g_privacy.scan_btn, TRUE);
        if (g_privacy.data_store) {
            for (const mustela::WebsiteDataEntry& entry : entries) {
                GtkTreeIter iter;
                gtk_list_store_append(g_privacy.data_store, &iter);
                gtk_list_store_set(g_privacy.data_store, &iter,
                    DC_ORIGIN, entry.origin.c_str(),
                    DC_KINDS, kinds_text(entry.kinds).c_str(), -1);
            }
        }
        set_data_status((std::to_string(entries.size()) + " origins with stored data").c_str());
    });
}

void on_remove_origin_clicked(GtkButton*, gpointer) {
    if (!g_privacy.b || !g_privacy.b->session || !g_privacy.data_store) return;
    std::string origin;
    GtkTreeSelection* selection = g_privacy.b->privacy_window
        ? gtk_tree_view_get_selection(GTK_TREE_VIEW(g_object_get_data(
              G_OBJECT(g_privacy.b->privacy_window), "kiwi-data-tree")))
        : nullptr;
    if (selection) {
        GtkTreeModel* model = nullptr;
        GtkTreeIter iter;
        if (gtk_tree_selection_get_selected(selection, &model, &iter)) {
            gchar* text = nullptr;
            gtk_tree_model_get(model, &iter, DC_ORIGIN, &text, -1);
            origin = text ? text : "";
            g_free(text);
        }
    }
    if (origin.empty()) {
        set_data_status("Select an origin to remove");
        return;
    }
    gtk_widget_set_sensitive(g_privacy.remove_btn, FALSE);
    set_data_status(("Removing " + origin + "…").c_str());
    g_privacy.b->session->remove_website_data({origin}, mustela::DataKind::All, [] {
        if (!g_privacy.b) return;
        gtk_widget_set_sensitive(g_privacy.remove_btn, TRUE);
        set_data_status("Removed. Rescan to refresh the list.");
    });
}

// ---- window ----

void on_privacy_destroy(GtkWidget*, gpointer) {
    g_privacy = PrivacyPage();
}

void on_privacy_destroy_notify(Browser* b) {
    if (b) b->privacy_window = nullptr;
}

GtkWidget* build_sites_page(Browser& b) {
    g_privacy.sites_store = gtk_list_store_new(SC_N, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING,
        G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING);
    GtkWidget* tree = gtk_tree_view_new_with_model(GTK_TREE_MODEL(g_privacy.sites_store));
    g_object_unref(g_privacy.sites_store);
    g_object_set_data(G_OBJECT(b.privacy_window), "kiwi-site-tree", tree);

    GtkCellRenderer* renderer = gtk_cell_renderer_text_new();
    const char* titles[] = {"Host", "Zoom", "JS", "Cookies", "Fingerprint", "Permissions", "UA", "CSS"};
    for (int col = 0; col < SC_N; ++col) {
        gtk_tree_view_insert_column_with_attributes(GTK_TREE_VIEW(tree), -1, titles[col], renderer,
            "text", col, nullptr);
    }
    gtk_tree_view_column_set_expand(gtk_tree_view_get_column(GTK_TREE_VIEW(tree), SC_HOST), TRUE);

    GtkWidget* scroll = gtk_scrolled_window_new(nullptr, nullptr);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll), GTK_POLICY_AUTOMATIC,
        GTK_POLICY_AUTOMATIC);
    gtk_container_add(GTK_CONTAINER(scroll), tree);

    GtkWidget* add_btn = gtk_button_new_with_label("Add…");
    gtk_style_context_add_class(gtk_widget_get_style_context(add_btn), "kiwi-accent");
    GtkWidget* edit_btn = gtk_button_new_with_label("Edit…");
    GtkWidget* remove_btn = gtk_button_new_with_label("Remove");
    g_signal_connect(add_btn, "clicked", G_CALLBACK(on_site_add), nullptr);
    g_signal_connect(edit_btn, "clicked", G_CALLBACK(on_site_edit), nullptr);
    g_signal_connect(remove_btn, "clicked", G_CALLBACK(on_site_remove), nullptr);
    g_signal_connect(tree, "row-activated", G_CALLBACK(on_site_row_activated), nullptr);

    GtkWidget* buttons = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
    gtk_box_pack_start(GTK_BOX(buttons), add_btn, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(buttons), edit_btn, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(buttons), remove_btn, FALSE, FALSE, 0);

    GtkWidget* hint = gtk_label_new(
        "Per-site zoom, user agent, style sheet and engine rules. Dressing applies on next page load.");
    gtk_style_context_add_class(gtk_widget_get_style_context(hint), "kiwi-secondary");
    gtk_label_set_xalign(GTK_LABEL(hint), 0.0f);

    GtkWidget* page = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
    gtk_container_set_border_width(GTK_CONTAINER(page), 6);
    gtk_box_pack_start(GTK_BOX(page), hint, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(page), scroll, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(page), buttons, FALSE, FALSE, 0);
    return page;
}

GtkWidget* build_data_page(Browser& b) {
    GtkWidget* page = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
    gtk_container_set_border_width(GTK_CONTAINER(page), 6);

    GtkWidget* kinds_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    g_privacy.kind_cookies = gtk_check_button_new_with_label("Cookies");
    g_privacy.kind_cache = gtk_check_button_new_with_label("Cache");
    g_privacy.kind_local = gtk_check_button_new_with_label("Local storage");
    g_privacy.kind_idb = gtk_check_button_new_with_label("IndexedDB");
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(g_privacy.kind_cookies), TRUE);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(g_privacy.kind_cache), TRUE);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(g_privacy.kind_local), TRUE);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(g_privacy.kind_idb), TRUE);
    gtk_box_pack_start(GTK_BOX(kinds_row), gtk_label_new("Kinds:"), FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(kinds_row), g_privacy.kind_cookies, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(kinds_row), g_privacy.kind_cache, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(kinds_row), g_privacy.kind_local, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(kinds_row), g_privacy.kind_idb, FALSE, FALSE, 0);

    g_privacy.age_combo = gtk_combo_box_text_new();
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(g_privacy.age_combo), "All time");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(g_privacy.age_combo), "Last 24 hours");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(g_privacy.age_combo), "Last hour");
    gtk_combo_box_set_active(GTK_COMBO_BOX(g_privacy.age_combo), 0);

    g_privacy.clear_btn = gtk_button_new_with_label("Clear selected");
    gtk_style_context_add_class(gtk_widget_get_style_context(g_privacy.clear_btn), "kiwi-accent");
    g_signal_connect(g_privacy.clear_btn, "clicked", G_CALLBACK(on_clear_clicked), nullptr);

    GtkWidget* clear_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
    gtk_box_pack_start(GTK_BOX(clear_row), g_privacy.age_combo, FALSE, FALSE, 0);
    gtk_box_pack_end(GTK_BOX(clear_row), g_privacy.clear_btn, FALSE, FALSE, 0);

    GtkWidget* separator = gtk_separator_new(GTK_ORIENTATION_HORIZONTAL);

    g_privacy.scan_btn = gtk_button_new_with_label("Scan origins");
    g_signal_connect(g_privacy.scan_btn, "clicked", G_CALLBACK(on_scan_clicked), nullptr);
    g_privacy.data_status = gtk_label_new("Stored data lives on this profile. Private tabs leave no trace.");
    gtk_style_context_add_class(gtk_widget_get_style_context(g_privacy.data_status), "kiwi-secondary");
    gtk_label_set_xalign(GTK_LABEL(g_privacy.data_status), 0.0f);

    GtkWidget* scan_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
    gtk_box_pack_start(GTK_BOX(scan_row), g_privacy.scan_btn, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(scan_row), g_privacy.data_status, TRUE, TRUE, 4);

    g_privacy.data_store = gtk_list_store_new(DC_N, G_TYPE_STRING, G_TYPE_STRING);
    GtkWidget* tree = gtk_tree_view_new_with_model(GTK_TREE_MODEL(g_privacy.data_store));
    g_object_unref(g_privacy.data_store);
    g_object_set_data(G_OBJECT(b.privacy_window), "kiwi-data-tree", tree);
    GtkCellRenderer* renderer = gtk_cell_renderer_text_new();
    gtk_tree_view_insert_column_with_attributes(GTK_TREE_VIEW(tree), -1, "Origin", renderer,
        "text", DC_ORIGIN, nullptr);
    gtk_tree_view_insert_column_with_attributes(GTK_TREE_VIEW(tree), -1, "Stored kinds", renderer,
        "text", DC_KINDS, nullptr);
    gtk_tree_view_column_set_expand(gtk_tree_view_get_column(GTK_TREE_VIEW(tree), DC_KINDS), TRUE);

    GtkWidget* scroll = gtk_scrolled_window_new(nullptr, nullptr);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll), GTK_POLICY_AUTOMATIC,
        GTK_POLICY_AUTOMATIC);
    gtk_container_add(GTK_CONTAINER(scroll), tree);

    g_privacy.remove_btn = gtk_button_new_with_label("Remove selected origin");
    g_signal_connect(g_privacy.remove_btn, "clicked", G_CALLBACK(on_remove_origin_clicked), nullptr);

    GtkWidget* bottom = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
    gtk_box_pack_end(GTK_BOX(bottom), g_privacy.remove_btn, FALSE, FALSE, 0);

    gtk_box_pack_start(GTK_BOX(page), kinds_row, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(page), clear_row, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(page), separator, FALSE, FALSE, 2);
    gtk_box_pack_start(GTK_BOX(page), scan_row, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(page), scroll, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(page), bottom, FALSE, FALSE, 0);
    return page;
}

void open_privacy_window(Browser& b, const std::string& focus_host) {
    if (b.privacy_window) {
        gtk_window_present(GTK_WINDOW(b.privacy_window));
        g_privacy.focus_host = focus_host;
        refresh_site_list();
        return;
    }

    GtkWidget* window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    b.privacy_window = window;
    g_privacy.b = &b;
    g_privacy.focus_host = focus_host;
    gtk_window_set_title(GTK_WINDOW(window), "Privacy Center — Kiwi");
    gtk_window_set_default_size(GTK_WINDOW(window), 780, 560);
    gtk_window_set_transient_for(GTK_WINDOW(window), GTK_WINDOW(b.window));
    g_signal_connect(window, "destroy", G_CALLBACK(on_privacy_destroy), nullptr);
    g_signal_connect_swapped(window, "destroy", G_CALLBACK(on_privacy_destroy_notify), &b);

    GtkWidget* notebook = gtk_notebook_new();
    gtk_notebook_append_page(GTK_NOTEBOOK(notebook), build_sites_page(b),
        gtk_label_new("Site overrides"));
    gtk_notebook_append_page(GTK_NOTEBOOK(notebook), build_data_page(b),
        gtk_label_new("Stored data"));

    gtk_container_add(GTK_CONTAINER(window), notebook);
    gtk_widget_show_all(window);
    refresh_site_list();
    gtk_notebook_set_current_page(GTK_NOTEBOOK(notebook), 0);
}

// ---------------- Bookmarks (K3) ----------------

enum BookmarkCols {
    BM_TITLE,
    BM_URL,
    BM_ADDED
};

bool has_url_scheme(const std::string& url) {
    if (url.find("://") != std::string::npos) return true;
    static const char* schemes[] = {"about:", "data:", "file:", "javascript:", "mailto:", "blob:"};
    for (const char* scheme : schemes) {
        if (url.rfind(scheme, 0) == 0) return true;
    }
    return false;
}

void on_bookmarks_destroy(GtkWidget*, gpointer user_data) {
    Browser* b = static_cast<Browser*>(user_data);
    if (!b) return;
    b->bookmarks_window = nullptr;
    b->bookmarks_store = nullptr;
}

void update_bookmark_star(Browser& b) {
    if (!b.url_entry) return;
    KiwiTab* tab = active_tab(b);
    bool marked = tab && b.bookmarks && !tab->uri.empty() && b.bookmarks->contains(tab->uri);
    gtk_entry_set_icon_from_icon_name(GTK_ENTRY(b.url_entry), GTK_ENTRY_ICON_SECONDARY,
        marked ? "starred-symbolic" : "non-starred-symbolic");
}

void toggle_bookmark(Browser& b) {
    if (!b.bookmarks) return;
    KiwiTab* tab = active_tab(b);
    if (!tab || tab->uri.empty()) return;
    if (b.bookmarks->contains(tab->uri)) {
        b.bookmarks->remove(tab->uri);
        flash_status(b, "bookmark removed");
    } else {
        b.bookmarks->add(tab->uri, tab->title.empty() ? tab->uri : tab->title,
            static_cast<std::int64_t>(std::time(nullptr)));
        flash_status(b, "bookmark added");
    }
    refresh_bookmarks_window(b);
    update_bookmark_star(b);
}

// ---- bookmark editor ----

struct BookmarkEditor {
    Browser* b = nullptr;
    GtkWidget* dialog = nullptr;
    GtkWidget* url = nullptr;
    GtkWidget* title = nullptr;
    GtkWidget* hint = nullptr;
    std::string original_url;
    std::int64_t added_at = 0;
};

void on_bookmark_editor_destroy(GtkWidget*, gpointer user_data) {
    delete static_cast<BookmarkEditor*>(user_data);
}

void on_bookmark_editor_response(GtkDialog* dialog, gint response, gpointer user_data) {
    BookmarkEditor* e = static_cast<BookmarkEditor*>(user_data);
    if (response == GTK_RESPONSE_OK && e && e->b && e->b->bookmarks) {
        std::string url = trim_copy(gtk_entry_get_text(GTK_ENTRY(e->url)));
        std::string title = trim_copy(gtk_entry_get_text(GTK_ENTRY(e->title)));
        if (url.empty()) {
            gtk_label_set_text(GTK_LABEL(e->hint), "URL is required.");
            return;
        }
        if (!has_url_scheme(url)) url = "https://" + url;
        if (e->added_at == 0) e->added_at = static_cast<std::int64_t>(std::time(nullptr));
        if (!e->original_url.empty() && e->original_url != url) e->b->bookmarks->remove(e->original_url);
        e->b->bookmarks->add(url, title, e->added_at);
        refresh_bookmarks_window(*e->b);
        update_bookmark_star(*e->b);
    }
    gtk_widget_destroy(GTK_WIDGET(dialog));
}

void open_bookmark_editor(Browser& b, const std::string& url, const std::string& title,
    std::int64_t added_at) {
    BookmarkEditor* e = new BookmarkEditor;
    e->b = &b;
    e->original_url = url;
    e->added_at = added_at;
    e->dialog = gtk_dialog_new_with_buttons(
        url.empty() ? "Add bookmark — Kiwi" : "Edit bookmark — Kiwi",
        b.bookmarks_window ? GTK_WINDOW(b.bookmarks_window) : GTK_WINDOW(b.window),
        static_cast<GtkDialogFlags>(GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT),
        "_Cancel", GTK_RESPONSE_CANCEL,
        "_Save", GTK_RESPONSE_OK,
        nullptr);
    GtkWidget* save_btn = gtk_dialog_get_widget_for_response(GTK_DIALOG(e->dialog), GTK_RESPONSE_OK);
    if (save_btn) gtk_style_context_add_class(gtk_widget_get_style_context(save_btn), "kiwi-accent");
    g_signal_connect(e->dialog, "destroy", G_CALLBACK(on_bookmark_editor_destroy), e);
    g_signal_connect(e->dialog, "response", G_CALLBACK(on_bookmark_editor_response), e);

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

    e->url = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(e->url), "example.org/page");
    if (!url.empty()) gtk_entry_set_text(GTK_ENTRY(e->url), url.c_str());
    attach_row(0, "URL", e->url);

    e->title = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(e->title), "page title (optional)");
    if (!title.empty()) gtk_entry_set_text(GTK_ENTRY(e->title), title.c_str());
    attach_row(1, "Title", e->title);

    e->hint = gtk_label_new("https:// is added automatically when the scheme is missing.");
    gtk_style_context_add_class(gtk_widget_get_style_context(e->hint), "kiwi-secondary");
    gtk_label_set_xalign(GTK_LABEL(e->hint), 0.0f);

    GtkWidget* content = gtk_dialog_get_content_area(GTK_DIALOG(e->dialog));
    gtk_box_pack_start(GTK_BOX(content), grid, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(content), e->hint, FALSE, FALSE, 4);

    gtk_widget_show_all(e->dialog);
}

// ---- bookmarks window ----

std::string selected_bookmark_url(Browser& b) {
    if (!b.bookmarks_window) return std::string();
    GtkWidget* tree = static_cast<GtkWidget*>(g_object_get_data(
        G_OBJECT(b.bookmarks_window), "kiwi-bookmark-tree"));
    if (!tree) return std::string();
    GtkTreeSelection* selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(tree));
    GtkTreeModel* model = nullptr;
    GtkTreeIter iter;
    if (!gtk_tree_selection_get_selected(selection, &model, &iter)) return std::string();
    gchar* url = nullptr;
    gtk_tree_model_get(model, &iter, BM_URL, &url, -1);
    std::string out = url ? url : "";
    g_free(url);
    return out;
}

void refresh_bookmarks_window(Browser& b) {
    if (!b.bookmarks_store) return;
    gtk_list_store_clear(b.bookmarks_store);
    if (!b.bookmarks) return;
    for (const mustela::BookmarkEntry& entry : b.bookmarks->entries()) {
        GtkTreeIter iter;
        gtk_list_store_append(b.bookmarks_store, &iter);
        gtk_list_store_set(b.bookmarks_store, &iter,
            BM_TITLE, entry.title.c_str(),
            BM_URL, entry.url.c_str(),
            BM_ADDED, format_time(entry.added_at).c_str(), -1);
    }
}

void on_bookmark_row_activated(GtkTreeView*, GtkTreePath*, GtkTreeViewColumn*, gpointer user_data) {
    Browser* b = static_cast<Browser*>(user_data);
    if (b) {
        std::string url = selected_bookmark_url(*b);
        if (!url.empty()) open_tab(*b, url, false);
    }
}

void on_bookmark_add(GtkButton*, gpointer user_data) {
    Browser* b = static_cast<Browser*>(user_data);
    if (b) open_bookmark_editor(*b, "", "", 0);
}

void on_bookmark_edit(GtkButton*, gpointer user_data) {
    Browser* b = static_cast<Browser*>(user_data);
    if (!b || !b->bookmarks) return;
    std::string url = selected_bookmark_url(*b);
    if (url.empty()) return;
    std::string title;
    std::int64_t added_at = 0;
    for (const mustela::BookmarkEntry& entry : b->bookmarks->entries()) {
        if (entry.url == url) {
            title = entry.title;
            added_at = entry.added_at;
            break;
        }
    }
    open_bookmark_editor(*b, url, title, added_at);
}

void on_bookmark_remove(GtkButton*, gpointer user_data) {
    Browser* b = static_cast<Browser*>(user_data);
    if (!b || !b->bookmarks) return;
    std::string url = selected_bookmark_url(*b);
    if (url.empty()) return;
    b->bookmarks->remove(url);
    refresh_bookmarks_window(*b);
    update_bookmark_star(*b);
}

void open_bookmarks_window(Browser& b) {
    if (b.bookmarks_window) {
        gtk_window_present(GTK_WINDOW(b.bookmarks_window));
        refresh_bookmarks_window(b);
        return;
    }

    GtkWidget* window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    b.bookmarks_window = window;
    gtk_window_set_title(GTK_WINDOW(window), "Bookmarks — Kiwi");
    gtk_window_set_default_size(GTK_WINDOW(window), 720, 480);
    gtk_window_set_transient_for(GTK_WINDOW(window), GTK_WINDOW(b.window));
    g_signal_connect(window, "destroy", G_CALLBACK(on_bookmarks_destroy), &b);

    b.bookmarks_store = gtk_list_store_new(3, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING);
    GtkWidget* tree = gtk_tree_view_new_with_model(GTK_TREE_MODEL(b.bookmarks_store));
    g_object_unref(b.bookmarks_store);
    g_object_set_data(G_OBJECT(window), "kiwi-bookmark-tree", tree);
    gtk_tree_view_set_headers_visible(GTK_TREE_VIEW(tree), TRUE);

    GtkCellRenderer* renderer = gtk_cell_renderer_text_new();
    gtk_tree_view_insert_column_with_attributes(GTK_TREE_VIEW(tree), -1, "Title", renderer,
        "text", BM_TITLE, nullptr);
    gtk_tree_view_insert_column_with_attributes(GTK_TREE_VIEW(tree), -1, "URL", renderer,
        "text", BM_URL, nullptr);
    gtk_tree_view_insert_column_with_attributes(GTK_TREE_VIEW(tree), -1, "Added", renderer,
        "text", BM_ADDED, nullptr);
    gtk_tree_view_column_set_expand(gtk_tree_view_get_column(GTK_TREE_VIEW(tree), BM_URL), TRUE);
    g_signal_connect(tree, "row-activated", G_CALLBACK(on_bookmark_row_activated), &b);

    GtkWidget* scroll = gtk_scrolled_window_new(nullptr, nullptr);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll), GTK_POLICY_AUTOMATIC,
        GTK_POLICY_AUTOMATIC);
    gtk_container_add(GTK_CONTAINER(scroll), tree);

    GtkWidget* add_btn = gtk_button_new_with_label("Add…");
    gtk_style_context_add_class(gtk_widget_get_style_context(add_btn), "kiwi-accent");
    GtkWidget* edit_btn = gtk_button_new_with_label("Edit…");
    GtkWidget* remove_btn = gtk_button_new_with_label("Remove");
    g_signal_connect(add_btn, "clicked", G_CALLBACK(on_bookmark_add), &b);
    g_signal_connect(edit_btn, "clicked", G_CALLBACK(on_bookmark_edit), &b);
    g_signal_connect(remove_btn, "clicked", G_CALLBACK(on_bookmark_remove), &b);

    GtkWidget* buttons = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
    gtk_box_pack_start(GTK_BOX(buttons), add_btn, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(buttons), edit_btn, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(buttons), remove_btn, FALSE, FALSE, 0);

    GtkWidget* hint = gtk_label_new(
        "Bookmarks live in the active profile. Ctrl+D bookmarks the current page.");
    gtk_style_context_add_class(gtk_widget_get_style_context(hint), "kiwi-secondary");
    gtk_label_set_xalign(GTK_LABEL(hint), 0.0f);

    GtkWidget* bottom = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
    gtk_box_pack_start(GTK_BOX(bottom), hint, TRUE, TRUE, 0);
    gtk_box_pack_end(GTK_BOX(bottom), buttons, FALSE, FALSE, 0);

    GtkWidget* layout = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
    gtk_container_set_border_width(GTK_CONTAINER(layout), 4);
    gtk_box_pack_start(GTK_BOX(layout), scroll, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(layout), bottom, FALSE, FALSE, 0);
    gtk_container_add(GTK_CONTAINER(window), layout);

    gtk_widget_show_all(window);
    refresh_bookmarks_window(b);
}
