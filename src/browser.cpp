#include "browser.hpp"

#include <glib/gstdio.h>

#include <cstring>
#include <ctime>
#include <string>

#include "mustela/mustela.hpp"
#include "mustela/view.hpp"

#include "viewhost.hpp"

KiwiTab* active_tab(Browser& b);

namespace {

Browser* browser = nullptr;

gint tab_index(const Browser& b, const KiwiTab* tab) {
    for (size_t i = 0; i < b.tabs.size(); ++i) {
        if (b.tabs[i].get() == tab) return static_cast<gint>(i);
    }
    return -1;
}

KiwiTab* tab_from_widget(Browser& b, GtkWidget* widget) {
    for (std::unique_ptr<KiwiTab>& tab : b.tabs) {
        if (tab->host && tab->host == widget) return tab.get();
    }
    return nullptr;
}

std::string favicon_path_for(Browser& b, const std::string& uri) {
    std::string host = mustela::host_from_uri(uri);
    std::string name = mustela::sanitize_filename(host);
    if (host.empty() || name.empty() || !b.profile) return std::string();
    return b.profile->paths().favicons + "/" + name + ".png";
}

void set_tab_icon(Browser& b, KiwiTab* tab, const std::string& uri) {
    if (!tab || !tab->icon) return;
    std::string path = favicon_path_for(b, uri);
    bool loaded = false;
    if (!path.empty() && g_file_test(path.c_str(), G_FILE_TEST_EXISTS)) {
        GdkPixbuf* pixbuf = gdk_pixbuf_new_from_file_at_scale(path.c_str(), 16, 16, TRUE, nullptr);
        if (pixbuf) {
            gtk_image_set_from_pixbuf(GTK_IMAGE(tab->icon), pixbuf);
            g_object_unref(pixbuf);
            loaded = true;
        }
    }
    if (!loaded) gtk_image_clear(GTK_IMAGE(tab->icon));
}

std::string tab_label_text(const KiwiTab& tab) {
    std::string text = tab.title;
    if (tab.private_tab) text = "private · " + text;
    return text;
}

void update_tab_label(Browser& b, KiwiTab* tab) {
    if (!tab || !tab->title_label) return;
    gtk_label_set_text(GTK_LABEL(tab->title_label), tab_label_text(*tab).c_str());
    gtk_widget_set_tooltip_text(tab->label, tab->uri.c_str());
    set_tab_icon(b, tab, tab->uri);
}

void refresh_status(Browser& b) {
    if (!b.status_label) return;
    if (!b.hover_link.empty()) {
        gtk_label_set_text(GTK_LABEL(b.status_label), ("→ " + b.hover_link).c_str());
        return;
    }
    KiwiTab* tab = active_tab(b);
    std::string text;
    if (tab && tab->private_tab) text = "private · ";
    text += std::string("cookies ") + mustela::cookie_policy_name(b.privacy.cookies);
    if (b.privacy.hardening.tracking_prevention) text += " · itp";
    if (b.privacy.fingerprint.active()) text += " · fp";
    if (!b.downloads.empty()) {
        std::string line;
        auto it = b.downloads.rbegin();
        if (it != b.downloads.rend() && b.downloads.size() == 1) {
            const DownloadRow& row = it->second;
            line = "↓ " + row.name + " "
                + std::to_string(static_cast<int>(row.progress * 100.0)) + "%";
        } else {
            line = "↓ " + std::to_string(b.downloads.size()) + " downloads";
        }
        text = line + " · " + text;
    }
    gtk_label_set_text(GTK_LABEL(b.status_label), text.c_str());
}

gboolean restore_status(gpointer) {
    if (browser) refresh_chrome(*browser);
    return G_SOURCE_REMOVE;
}

std::string sanitize_label_text(const std::string& raw) {
    std::string text = mustela::sanitize_dialog_text(raw, 256);
    return text;
}

void run_js_alert(Browser& b, const std::string& message) {
    GtkWidget* dialog = gtk_message_dialog_new(GTK_WINDOW(b.window), GTK_DIALOG_MODAL,
        GTK_MESSAGE_WARNING, GTK_BUTTONS_OK, "%s", message.c_str());
    gtk_window_set_title(GTK_WINDOW(dialog), "JavaScript — Kiwi");
    gtk_dialog_run(GTK_DIALOG(dialog));
    gtk_widget_destroy(dialog);
}

bool run_js_confirm(Browser& b, const std::string& message) {
    GtkWidget* dialog = gtk_message_dialog_new(GTK_WINDOW(b.window), GTK_DIALOG_MODAL,
        GTK_MESSAGE_QUESTION, GTK_BUTTONS_YES_NO, "%s", message.c_str());
    gtk_window_set_title(GTK_WINDOW(dialog), "JavaScript — Kiwi");
    gint response = gtk_dialog_run(GTK_DIALOG(dialog));
    gtk_widget_destroy(dialog);
    return response == GTK_RESPONSE_YES;
}

std::string run_js_prompt(Browser& b, const std::string& message, const std::string& default_text,
    bool& ok) {
    GtkWidget* dialog = gtk_message_dialog_new(GTK_WINDOW(b.window), GTK_DIALOG_MODAL,
        GTK_MESSAGE_QUESTION, GTK_BUTTONS_OK_CANCEL, "%s", message.c_str());
    gtk_window_set_title(GTK_WINDOW(dialog), "JavaScript — Kiwi");
    GtkWidget* entry = gtk_entry_new();
    gtk_entry_set_text(GTK_ENTRY(entry), default_text.c_str());
    gtk_entry_set_activates_default(GTK_ENTRY(entry), TRUE);
    gtk_widget_set_margin_start(entry, 12);
    gtk_widget_set_margin_end(entry, 12);
    gtk_widget_set_margin_bottom(entry, 6);
    GtkWidget* area = gtk_message_dialog_get_message_area(GTK_MESSAGE_DIALOG(dialog));
    gtk_box_pack_start(GTK_BOX(area), entry, FALSE, FALSE, 4);
    gtk_dialog_set_default_response(GTK_DIALOG(dialog), GTK_RESPONSE_OK);
    gtk_widget_show_all(dialog);
    gint response = gtk_dialog_run(GTK_DIALOG(dialog));
    std::string text = gtk_entry_get_text(GTK_ENTRY(entry));
    gtk_widget_destroy(dialog);
    ok = response == GTK_RESPONSE_OK;
    return text;
}

enum PermResponse {
    PERM_ALLOW = 1,
    PERM_DENY,
    PERM_ALWAYS,
    PERM_NEVER
};

mustela::PermissionAnswer run_permission_prompt(Browser& b, const std::string& host,
    const std::string& what) {
    GtkWidget* dialog = gtk_message_dialog_new(GTK_WINDOW(b.window), GTK_DIALOG_MODAL,
        GTK_MESSAGE_QUESTION, GTK_BUTTONS_NONE, "%s wants to use %s", host.c_str(), what.c_str());
    gtk_window_set_title(GTK_WINDOW(dialog), "Permission — Kiwi");
    gtk_dialog_add_buttons(GTK_DIALOG(dialog),
        "Allow", PERM_ALLOW,
        "Deny", PERM_DENY,
        "Always allow", PERM_ALWAYS,
        "Never allow", PERM_NEVER,
        nullptr);
    gint response = gtk_dialog_run(GTK_DIALOG(dialog));
    gtk_widget_destroy(dialog);
    switch (response) {
        case PERM_ALLOW: return mustela::PermissionAnswer::Allow;
        case PERM_ALWAYS: return mustela::PermissionAnswer::Always;
        case PERM_NEVER: return mustela::PermissionAnswer::Never;
        default: return mustela::PermissionAnswer::Deny;
    }
}

bool run_certificate_prompt(Browser& b, const std::string& host) {
    std::string message = "The certificate for " + host + " is invalid. Continue anyway?";
    GtkWidget* dialog = gtk_message_dialog_new(GTK_WINDOW(b.window), GTK_DIALOG_MODAL,
        GTK_MESSAGE_WARNING, GTK_BUTTONS_YES_NO, "%s", message.c_str());
    gtk_window_set_title(GTK_WINDOW(dialog), "Invalid certificate — Kiwi");
    gint response = gtk_dialog_run(GTK_DIALOG(dialog));
    gtk_widget_destroy(dialog);
    return response == GTK_RESPONSE_YES;
}

bool run_auth_prompt(Browser& b, const std::string& label, std::string& user, std::string& password) {
    GtkWidget* dialog = gtk_message_dialog_new(GTK_WINDOW(b.window), GTK_DIALOG_MODAL,
        GTK_MESSAGE_QUESTION, GTK_BUTTONS_OK_CANCEL, "%s", ("Sign in to " + label).c_str());
    gtk_window_set_title(GTK_WINDOW(dialog), "Authentication — Kiwi");
    GtkWidget* user_entry = gtk_entry_new();
    GtkWidget* pass_entry = gtk_entry_new();
    gtk_entry_set_visibility(GTK_ENTRY(pass_entry), FALSE);
    gtk_entry_set_activates_default(GTK_ENTRY(user_entry), TRUE);
    gtk_entry_set_activates_default(GTK_ENTRY(pass_entry), TRUE);
    gtk_widget_set_margin_start(user_entry, 12);
    gtk_widget_set_margin_end(user_entry, 12);
    gtk_widget_set_margin_bottom(user_entry, 6);
    gtk_widget_set_margin_start(pass_entry, 12);
    gtk_widget_set_margin_end(pass_entry, 12);
    gtk_widget_set_margin_bottom(pass_entry, 6);
    GtkWidget* area = gtk_message_dialog_get_message_area(GTK_MESSAGE_DIALOG(dialog));
    gtk_box_pack_start(GTK_BOX(area), user_entry, FALSE, FALSE, 4);
    gtk_box_pack_start(GTK_BOX(area), pass_entry, FALSE, FALSE, 4);
    gtk_dialog_set_default_response(GTK_DIALOG(dialog), GTK_RESPONSE_OK);
    gtk_widget_show_all(dialog);
    gint response = gtk_dialog_run(GTK_DIALOG(dialog));
    user = gtk_entry_get_text(GTK_ENTRY(user_entry));
    password = gtk_entry_get_text(GTK_ENTRY(pass_entry));
    gtk_widget_destroy(dialog);
    return response == GTK_RESPONSE_OK;
}

void on_close_tab_clicked(GtkButton*, gpointer user_data) {
    close_tab(*browser, static_cast<KiwiTab*>(user_data));
}

GtkWidget* build_tab_label(Browser&, KiwiTab* tab) {
    GtkWidget* box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
    GtkWidget* icon = gtk_image_new();
    tab->icon = icon;
    tab->title_label = gtk_label_new(tab_label_text(*tab).c_str());
    gtk_label_set_ellipsize(GTK_LABEL(tab->title_label), PANGO_ELLIPSIZE_END);
    gtk_label_set_width_chars(GTK_LABEL(tab->title_label), 18);
    gtk_label_set_xalign(GTK_LABEL(tab->title_label), 0.0f);

    GtkWidget* close_btn = gtk_button_new_from_icon_name("window-close-symbolic", GTK_ICON_SIZE_MENU);
    gtk_button_set_relief(GTK_BUTTON(close_btn), GTK_RELIEF_NONE);
    gtk_widget_set_valign(close_btn, GTK_ALIGN_CENTER);
    g_signal_connect(close_btn, "clicked", G_CALLBACK(on_close_tab_clicked), tab);

    gtk_box_pack_start(GTK_BOX(box), icon, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(box), tab->title_label, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(box), close_btn, FALSE, FALSE, 0);
    gtk_widget_show_all(box);
    return box;
}

enum TabDropTarget {
    TAB_DROP_URI_LIST = 1001,
    TAB_DROP_TEXT = 1002
};

std::string drop_url_from_selection(GtkSelectionData* data, guint info) {
    const guchar* raw = gtk_selection_data_get_data(data);
    gint len = gtk_selection_data_get_length(data);
    if (!raw || len <= 0) return std::string();
    std::string text(reinterpret_cast<const char*>(raw), static_cast<size_t>(len));
    if (info == TAB_DROP_URI_LIST) {
        size_t end = text.find_first_of("\r\n");
        if (end != std::string::npos) text = text.substr(0, end);
        if (text.rfind("http://", 0) == 0 || text.rfind("https://", 0) == 0
            || text.rfind("file://", 0) == 0 || text.rfind("about:", 0) == 0) {
            return text;
        }
        return std::string();
    }
    std::string trimmed = mustela::sanitize_dialog_text(text, 2048);
    size_t start = trimmed.find_first_not_of(" \t");
    if (start == std::string::npos) return std::string();
    trimmed = trimmed.substr(start);
    size_t finish = trimmed.find_last_not_of(" \t\r\n");
    if (finish != std::string::npos) trimmed = trimmed.substr(0, finish + 1);
    if (trimmed.rfind("http://", 0) == 0 || trimmed.rfind("https://", 0) == 0
        || trimmed.rfind("file://", 0) == 0) {
        return trimmed;
    }
    return std::string();
}

void on_tab_drop_received(GtkWidget*, GdkDragContext* context, gint, gint,
    GtkSelectionData* data, guint info, guint time, gpointer user_data) {
    KiwiTab* tab = static_cast<KiwiTab*>(user_data);
    if (!browser) {
        gtk_drag_finish(context, FALSE, FALSE, time);
        return;
    }
    std::string url = drop_url_from_selection(data, info);
    if (url.empty()) {
        gtk_drag_finish(context, FALSE, FALSE, time);
        return;
    }
    if (tab) {
        tab->view->load_url(url);
    } else {
        open_tab(*browser, url);
    }
    refresh_chrome(*browser);
    gtk_drag_finish(context, TRUE, FALSE, time);
}

void wire_tab_drops(KiwiTab* tab) {
    if (!tab || !tab->title_label) return;
    GtkTargetEntry entries[] = {
        {(gchar*)"text/uri-list", 0, TAB_DROP_URI_LIST},
        {(gchar*)"UTF8_STRING", 0, TAB_DROP_TEXT},
        {(gchar*)"text/plain;charset=utf-8", 0, TAB_DROP_TEXT}};
    gtk_drag_dest_set(tab->title_label, GTK_DEST_DEFAULT_DROP, entries,
        G_N_ELEMENTS(entries), static_cast<GdkDragAction>(GDK_ACTION_COPY | GDK_ACTION_MOVE));
    g_signal_connect(tab->title_label, "drag-data-received", G_CALLBACK(on_tab_drop_received), tab);
}

void wire_notebook_drops(GtkWidget* notebook) {
    if (!notebook) return;
    GtkTargetEntry entries[] = {
        {(gchar*)"text/uri-list", 0, TAB_DROP_URI_LIST},
        {(gchar*)"UTF8_STRING", 0, TAB_DROP_TEXT},
        {(gchar*)"text/plain;charset=utf-8", 0, TAB_DROP_TEXT}};
    gtk_drag_dest_set(notebook, GTK_DEST_DEFAULT_DROP, entries,
        G_N_ELEMENTS(entries), static_cast<GdkDragAction>(GDK_ACTION_COPY | GDK_ACTION_MOVE));
    g_signal_connect(notebook, "drag-data-received", G_CALLBACK(on_tab_drop_received), nullptr);
}

void update_window_title(Browser& b) {
    KiwiTab* tab = active_tab(b);
    if (!tab) {
        gtk_window_set_title(GTK_WINDOW(b.window), "Kiwi");
        return;
    }
    gtk_window_set_title(GTK_WINDOW(b.window), (tab->title + " — Kiwi").c_str());
}

void record_history(Browser& b, KiwiTab* tab) {
    if (!b.history || !b.privacy.records_history(tab->private_tab ? mustela::TabPrivacy::Private
                                                                  : mustela::TabPrivacy::Normal)) {
        return;
    }
    std::string uri = tab->uri;
    if (!uri.empty()) {
        b.history->record(uri, tab->title, static_cast<std::int64_t>(std::time(nullptr)));
    }
}

void on_switch_page(GtkNotebook*, GtkWidget*, guint, gpointer) {
    gtk_entry_set_progress_fraction(GTK_ENTRY(browser->url_entry), 0.0);
    browser->hover_link.clear();
    toggle_find_bar(*browser, false);
    refresh_chrome(*browser);
}

void on_page_removed(GtkNotebook*, GtkWidget*, gpointer) {
    refresh_chrome(*browser);
}

void on_page_reordered(GtkNotebook*, GtkWidget* child, guint page_num, gpointer) {
    Browser& b = *browser;
    KiwiTab* tab = tab_from_widget(b, child);
    if (!tab) return;
    gint old = tab_index(b, tab);
    if (old < 0 || old == static_cast<gint>(page_num)) return;
    std::unique_ptr<KiwiTab> moved = std::move(b.tabs[static_cast<size_t>(old)]);
    b.tabs.erase(b.tabs.begin() + old);
    b.tabs.insert(b.tabs.begin() + page_num, std::move(moved));
}

void on_go_back(GtkButton*, gpointer) {
    KiwiTab* tab = active_tab(*browser);
    if (tab) tab->view->go_back();
}

void on_go_forward(GtkButton*, gpointer) {
    KiwiTab* tab = active_tab(*browser);
    if (tab) tab->view->go_forward();
}

void on_reload(GtkButton*, gpointer) {
    KiwiTab* tab = active_tab(*browser);
    if (tab) tab->view->reload();
}

void on_new_tab(GtkButton*, gpointer) {
    open_tab(*browser, browser->settings.homepage, browser->privacy.default_private
        == mustela::TabPrivacy::Private);
}

void on_private_toggled(GtkToggleButton* toggle, gpointer) {
    if (!browser) return;
    bool active = gtk_toggle_button_get_active(toggle) != FALSE;
    browser->privacy.default_private = active ? mustela::TabPrivacy::Private
                                              : mustela::TabPrivacy::Normal;
}

void on_url_activate(GtkEntry*, gpointer) {
    navigate_active(*browser, gtk_entry_get_text(GTK_ENTRY(browser->url_entry)));
}

void on_downloads_clicked(GtkButton*, gpointer) {
    open_downloads_window(*browser);
}

void on_bookmark_icon_pressed(GtkEntry*, GtkEntryIconPosition, GdkEventButton*, gpointer) {
    toggle_bookmark(*browser);
}

void on_settings_clicked(GtkButton*, gpointer) {
    open_settings_window(*browser);
}

void on_find_changed(GtkSearchEntry*, gpointer) {
    Browser& b = *browser;
    b.find_query = gtk_entry_get_text(GTK_ENTRY(b.find_entry));
    KiwiTab* tab = active_tab(b);
    if (!tab) return;
    if (b.find_query.empty()) {
        tab->view->find_finish();
        gtk_label_set_text(GTK_LABEL(b.find_hint), "");
    } else {
        tab->view->find(b.find_query);
    }
}

void on_find_activate(GtkEntry*, gpointer) {
    KiwiTab* tab = active_tab(*browser);
    if (tab && !browser->find_query.empty()) tab->view->find_next();
}

void on_find_next_clicked(GtkButton*, gpointer) {
    KiwiTab* tab = active_tab(*browser);
    if (tab && !browser->find_query.empty()) tab->view->find_next();
}

void on_find_previous_clicked(GtkButton*, gpointer) {
    KiwiTab* tab = active_tab(*browser);
    if (tab && !browser->find_query.empty()) tab->view->find_previous();
}

void on_find_stop(GtkSearchEntry*, gpointer) {
    toggle_find_bar(*browser, false);
}

void on_privacy_clicked(GtkButton*, gpointer) {
    if (!browser) return;
    std::string host;
    KiwiTab* tab = active_tab(*browser);
    if (tab && tab->view) host = mustela::host_from_uri(tab->uri);
    open_privacy_window(*browser, host);
}

std::string downloads_start_dir(const Browser& b) {
    if (!b.settings.downloads_dir.empty()) return b.settings.downloads_dir;
    const gchar* downloads = g_get_user_special_dir(G_USER_DIRECTORY_DOWNLOAD);
    return downloads ? std::string(downloads) : std::string(g_get_home_dir());
}

void save_page_as_mhtml(Browser& b) {
    KiwiTab* tab = active_tab(b);
    if (!tab || !tab->view || tab->uri.empty()) return;
    std::string name = mustela::suggested_save_name(tab->title, tab->uri);
    GtkWidget* chooser = gtk_file_chooser_dialog_new("Save page — Kiwi",
        GTK_WINDOW(b.window), GTK_FILE_CHOOSER_ACTION_SAVE,
        "Cancel", GTK_RESPONSE_CANCEL, "Save", GTK_RESPONSE_ACCEPT, nullptr);
    gtk_file_chooser_set_do_overwrite_confirmation(GTK_FILE_CHOOSER(chooser), TRUE);
    gtk_file_chooser_set_current_folder(GTK_FILE_CHOOSER(chooser), downloads_start_dir(b).c_str());
    gtk_file_chooser_set_current_name(GTK_FILE_CHOOSER(chooser), name.c_str());
    GtkFileFilter* filter = gtk_file_filter_new();
    gtk_file_filter_set_name(filter, "Web page (MHTML)");
    gtk_file_filter_add_pattern(filter, "*.mhtml");
    gtk_file_filter_add_pattern(filter, "*.mht");
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
    std::string shown = path;
    tab->view->save_mhtml(path, [&b, shown](bool ok) {
        flash_status(b, ok ? "page saved to " + shown : std::string("page save failed"));
    });
}

void show_kiwi_notification(Browser& b, const std::string& title, const std::string& body) {
    if (!b.gapp) return;
    std::string shown = title.empty() ? std::string("Kiwi") : title;
    GNotification* item = g_notification_new(shown.c_str());
    if (!body.empty()) g_notification_set_body(item, body.c_str());
    std::string key = "kiwi-notification-" + std::to_string(++b.notification_seq);
    g_application_send_notification(b.gapp, key.c_str(), item);
    g_object_unref(item);
}

void toggle_window_fullscreen(Browser& b) {
    if (!b.window || !gtk_widget_get_window(b.window)) return;
    GdkWindowState state = gdk_window_get_state(gtk_widget_get_window(b.window));
    if (state & GDK_WINDOW_STATE_FULLSCREEN) gtk_window_unfullscreen(GTK_WINDOW(b.window));
    else gtk_window_fullscreen(GTK_WINDOW(b.window));
}

gboolean on_key_press(GtkWidget*, GdkEventKey* event, gpointer) {
    Browser& b = *browser;
    GdkModifierType mods = static_cast<GdkModifierType>(event->state & gtk_accelerator_get_default_mod_mask());
    return kiwi_shortcut(b, event->keyval, mods) ? TRUE : FALSE;
}

bool run_shortcut(Browser& b, guint keyval, GdkModifierType mods) {
    KiwiTab* tab = active_tab(b);

    if (mods == GDK_CONTROL_MASK && keyval == GDK_KEY_t) {
        open_tab(b, b.settings.homepage, b.privacy.default_private == mustela::TabPrivacy::Private);
        return TRUE;
    }
    if (mods == (GDK_CONTROL_MASK | GDK_SHIFT_MASK) && keyval == GDK_KEY_P) {
        open_tab(b, b.settings.homepage, true);
        return TRUE;
    }
    if (mods == GDK_CONTROL_MASK && keyval == GDK_KEY_w) {
        if (tab) close_tab(b, tab);
        return TRUE;
    }
    if (mods == GDK_CONTROL_MASK && keyval == GDK_KEY_l) {
        gtk_widget_grab_focus(b.url_entry);
        return TRUE;
    }
    if (mods == GDK_CONTROL_MASK && keyval == GDK_KEY_f) {
        toggle_find_bar(b, true);
        return TRUE;
    }
    if (mods == 0 && keyval == GDK_KEY_F3) {
        if (tab && !b.find_query.empty()) tab->view->find_next();
        return TRUE;
    }
    if (mods == GDK_SHIFT_MASK && keyval == GDK_KEY_F3) {
        if (tab && !b.find_query.empty()) tab->view->find_previous();
        return TRUE;
    }
    if (mods == (GDK_CONTROL_MASK | GDK_SHIFT_MASK) && keyval == GDK_KEY_I) {
        toggle_devtools(b);
        return TRUE;
    }
    if (mods == GDK_CONTROL_MASK && keyval == GDK_KEY_h) {
        open_history_window(b);
        return TRUE;
    }
    if (mods == GDK_CONTROL_MASK && keyval == GDK_KEY_j) {
        open_downloads_window(b);
        return TRUE;
    }
    if (mods == GDK_CONTROL_MASK && keyval == GDK_KEY_d) {
        toggle_bookmark(b);
        return TRUE;
    }
    if (mods == (GDK_CONTROL_MASK | GDK_SHIFT_MASK) && keyval == GDK_KEY_O) {
        open_bookmarks_window(b);
        return TRUE;
    }
    if (mods == GDK_CONTROL_MASK && keyval == GDK_KEY_comma) {
        open_settings_window(b);
        return TRUE;
    }
    if (mods == GDK_CONTROL_MASK && keyval == GDK_KEY_r) {
        if (tab) tab->view->reload();
        return TRUE;
    }
    if (mods == 0 && keyval == GDK_KEY_F5) {
        if (tab) tab->view->reload();
        return TRUE;
    }
    if (mods == GDK_MOD1_MASK && keyval == GDK_KEY_Left) {
        if (tab) tab->view->go_back();
        return TRUE;
    }
    if (mods == GDK_MOD1_MASK && keyval == GDK_KEY_Right) {
        if (tab) tab->view->go_forward();
        return TRUE;
    }
    if (mods == GDK_CONTROL_MASK
        && (keyval == GDK_KEY_plus || keyval == GDK_KEY_equal
            || keyval == GDK_KEY_KP_Add)) {
        if (tab) tab->view->set_zoom(mustela::zoom_step(tab->view->zoom(), 1));
        return TRUE;
    }
    if (mods == GDK_CONTROL_MASK
        && (keyval == GDK_KEY_minus || keyval == GDK_KEY_KP_Subtract)) {
        if (tab) tab->view->set_zoom(mustela::zoom_step(tab->view->zoom(), -1));
        return TRUE;
    }
    if (mods == GDK_CONTROL_MASK && (keyval == GDK_KEY_0 || keyval == GDK_KEY_KP_0)) {
        if (tab) tab->view->set_zoom(1.0);
        return TRUE;
    }
    if (mods == GDK_CONTROL_MASK && keyval == GDK_KEY_q) {
        gtk_widget_destroy(b.window);
        return TRUE;
    }
    if (mods == GDK_CONTROL_MASK && keyval == GDK_KEY_s) {
        save_page_as_mhtml(b);
        return TRUE;
    }
    if (mods == 0 && keyval == GDK_KEY_F11) {
        toggle_window_fullscreen(b);
        return TRUE;
    }
    return FALSE;
}

void on_destroy(GtkWidget*, gpointer) {
    if (browser) {
        gint width = 0;
        gint height = 0;
        gtk_window_get_size(GTK_WINDOW(browser->window), &width, &height);
        browser->settings.window_width = width;
        browser->settings.window_height = height;
        gchar* path = g_build_filename(g_get_user_config_dir(), "kiwi", "settings.toml", nullptr);
        mustela::save_settings(path, browser->settings);
        g_free(path);
    }
    gtk_main_quit();
}

}

void flash_status(Browser& b, const std::string& text) {
    if (!b.status_label) return;
    gtk_label_set_text(GTK_LABEL(b.status_label), text.c_str());
    g_timeout_add_seconds(3, restore_status, nullptr);
}

void toggle_find_bar(Browser& b, bool show) {
    if (!b.find_bar) return;
    if (show) {
        if (!gtk_widget_get_visible(b.find_bar)) gtk_widget_show_all(b.find_bar);
        gtk_label_set_text(GTK_LABEL(b.find_hint), "");
        std::string query = gtk_entry_get_text(GTK_ENTRY(b.find_entry));
        b.find_query = query;
        gtk_widget_grab_focus(b.find_entry);
        KiwiTab* tab = active_tab(b);
        if (tab && !query.empty()) tab->view->find(query);
        return;
    }
    if (!gtk_widget_get_visible(b.find_bar)) return;
    KiwiTab* tab = active_tab(b);
    if (tab) tab->view->find_finish();
    b.find_query.clear();
    gtk_label_set_text(GTK_LABEL(b.find_hint), "");
    gtk_widget_hide(b.find_bar);
}

void toggle_devtools(Browser& b) {
    KiwiTab* tab = active_tab(b);
    if (!tab) return;
    if (tab->view->devtools_attached()) tab->view->hide_devtools();
    else tab->view->show_devtools();
}

KiwiTab* active_tab(Browser& b) {
    gint page = gtk_notebook_get_current_page(GTK_NOTEBOOK(b.notebook));
    if (page < 0 || page >= static_cast<gint>(b.tabs.size())) return nullptr;
    return b.tabs[static_cast<size_t>(page)].get();
}

void refresh_chrome(Browser& b) {
    KiwiTab* tab = active_tab(b);
    if (tab) {
        if (!gtk_widget_has_focus(b.url_entry)) {
            gtk_entry_set_text(GTK_ENTRY(b.url_entry), tab->uri.c_str());
        }
        gtk_widget_set_sensitive(b.back_btn, tab->view->can_go_back());
        gtk_widget_set_sensitive(b.forward_btn, tab->view->can_go_forward());
        gtk_widget_set_sensitive(b.reload_btn, !tab->uri.empty());
        update_tab_label(b, tab);
    }
    gtk_notebook_set_show_tabs(GTK_NOTEBOOK(b.notebook), gtk_notebook_get_n_pages(GTK_NOTEBOOK(b.notebook)) > 1);
    update_window_title(b);
    refresh_status(b);
    update_privacy_indicator(b);
    update_bookmark_star(b);
}

void navigate_active(Browser& b, const std::string& entry_text) {
    KiwiTab* tab = active_tab(b);
    if (!tab) return;
    std::string url = mustela::resolve_entry(entry_text, b.settings);
    if (!url.empty()) tab->view->load_url(url);
}

void close_tab(Browser& b, KiwiTab* tab) {
    gint idx = tab_index(b, tab);
    if (idx < 0) return;
    if (tab == active_tab(b)) toggle_find_bar(b, false);
    gtk_notebook_remove_page(GTK_NOTEBOOK(b.notebook), idx);
    b.tabs.erase(b.tabs.begin() + idx);
    if (b.tabs.empty()) {
        open_tab(b, b.settings.homepage, b.privacy.default_private == mustela::TabPrivacy::Private);
        return;
    }
    refresh_chrome(b);
}

mustela::ViewCallbacks make_tab_callbacks(Browser* b, KiwiTab* tab) {
    mustela::ViewCallbacks cb;
    cb.uri_changed = [b, tab](const std::string& uri) {
        tab->uri = uri;
        if (active_tab(*b) == tab) refresh_chrome(*b);
        else update_tab_label(*b, tab);
    };
    cb.title_changed = [b, tab](const std::string& title) {
        tab->title = sanitize_label_text(title);
        if (active_tab(*b) == tab) refresh_chrome(*b);
        else update_tab_label(*b, tab);
    };
    cb.load_progress = [b](double fraction) {
        gtk_entry_set_progress_fraction(GTK_ENTRY(b->url_entry), fraction >= 1.0 ? 0.0 : fraction);
    };
    cb.loading_changed = [b](bool loading) {
        if (!loading) gtk_entry_set_progress_fraction(GTK_ENTRY(b->url_entry), 0.0);
    };
    cb.committed = [b, tab](const std::string& uri, const std::string& title, bool private_mode) {
        tab->uri = uri;
        tab->title = sanitize_label_text(title);
        tab->private_tab = private_mode;
        record_history(*b, tab);
        refresh_chrome(*b);
    };
    cb.load_failed = [b](const std::string&, const std::string& heading, const std::string&) {
        flash_status(*b, heading);
    };
    cb.favicon_changed = [b, tab](const std::string& uri) {
        set_tab_icon(*b, tab, uri);
    };
    cb.link_hover = [b](const std::string& uri) {
        b->hover_link = uri;
        refresh_status(*b);
    };
    cb.web_process_gone = [b](const std::string& reason) {
        flash_status(*b, reason);
    };
    cb.script_alert = [b](const std::string& message) {
        run_js_alert(*b, message);
    };
    cb.script_confirm = [b](const std::string& message) {
        return run_js_confirm(*b, message);
    };
    cb.script_prompt = [b](const std::string& message, const std::string& default_text, bool& ok) {
        return run_js_prompt(*b, message, default_text, ok);
    };
    cb.permission_prompt = [b](const std::string& host, const std::string& what) {
        return run_permission_prompt(*b, host, what);
    };
    cb.certificate_prompt = [b](const std::string& host) {
        return run_certificate_prompt(*b, host);
    };
    cb.auth_prompt = [b](const std::string& label, std::string& user, std::string& password) {
        return run_auth_prompt(*b, label, user, password);
    };
    cb.popup_created = [b](mustela::View* popup) {
        KiwiTab* created = new KiwiTab();
        created->view.reset(popup);
        created->private_tab = popup->is_private();
        created->host = kiwi::view_page_widget(popup, GTK_WINDOW(b->window));
        b->tabs.emplace_back(created);
        gint idx = gtk_notebook_append_page(GTK_NOTEBOOK(b->notebook), created->host,
            build_tab_label(*b, created));
        gtk_notebook_set_tab_reorderable(GTK_NOTEBOOK(b->notebook), created->host, TRUE);
        gtk_widget_show(created->host);
        popup->set_callbacks(make_tab_callbacks(b, b->tabs.back().get()));
        (void)idx;
    };
    cb.popup_ready = [b](mustela::View* popup) {
        for (std::unique_ptr<KiwiTab>& tab : b->tabs) {
            if (tab->view.get() == popup && tab->host) {
                gint idx = gtk_notebook_page_num(GTK_NOTEBOOK(b->notebook), tab->host);
                if (idx >= 0) gtk_notebook_set_current_page(GTK_NOTEBOOK(b->notebook), idx);
                break;
            }
        }
        refresh_chrome(*b);
    };
    cb.close_requested = [b, tab]() {
        close_tab(*b, tab);
    };
    cb.context_menu = [b](const mustela::ContextMenuInfo& info) {
        open_context_menu(*b, info);
        return true;
    };
    cb.find_result = [b](bool found) {
        if (b->find_hint && b->find_bar && gtk_widget_get_visible(b->find_bar)) {
            gtk_label_set_text(GTK_LABEL(b->find_hint), found ? "found" : "no matches");
        }
    };
    cb.devtools_docked = [b](bool docked) {
        flash_status(*b, docked ? "devtools docked — Ctrl+Shift+I to hide" : "devtools hidden");
    };
    cb.enter_fullscreen = [b]() {
        if (b->window) gtk_window_fullscreen(GTK_WINDOW(b->window));
    };
    cb.leave_fullscreen = [b]() {
        if (b->window) gtk_window_unfullscreen(GTK_WINDOW(b->window));
    };
    cb.notification = [b](const std::string& title, const std::string& body) {
        show_kiwi_notification(*b, title, body);
    };
    return cb;
}

KiwiTab* open_tab(Browser& b, const std::string& url, bool force_private) {
    bool priv = force_private || b.privacy.tab_privacy(mustela::TabPrivacy::Normal)
        == mustela::TabPrivacy::Private;
    KiwiTab* tab = new KiwiTab();
    tab->private_tab = priv;
    tab->view = std::make_unique<mustela::View>(*b.session, priv, make_tab_callbacks(&b, tab));
    tab->view->set_zoom(b.settings.zoom_default);
    tab->label = build_tab_label(b, tab);
    b.tabs.emplace_back(tab);

    tab->host = kiwi::view_page_widget(tab->view.get(), GTK_WINDOW(b.window));
    gint idx = gtk_notebook_append_page(GTK_NOTEBOOK(b.notebook), tab->host, tab->label);
    gtk_notebook_set_tab_reorderable(GTK_NOTEBOOK(b.notebook), tab->host, TRUE);
    wire_tab_drops(tab);
    gtk_notebook_set_current_page(GTK_NOTEBOOK(b.notebook), idx);
    gtk_widget_show(tab->host);

    if (!url.empty()) tab->view->load_url(url);
    refresh_chrome(b);
    return tab;
}

bool kiwi_shortcut(Browser& b, guint keyval, GdkModifierType mods) {
    return run_shortcut(b, keyval, mods);
}

void build_browser_ui(Browser& b, const char* startup_url) {
    browser = &b;

    mustela::ViewConfig config;
    config.settings = &b.settings;
    config.privacy = &b.privacy;
    config.sites = b.sites.get();
    config.profile = b.profile.get();
    mustela::SessionCallbacks session_cb;
    session_cb.download_destination = [&b](const std::string& suggested_name) {
        if (b.settings.downloads_mode != "ask") return std::string();
        GtkWidget* chooser = gtk_file_chooser_dialog_new("Save download — Kiwi",
            GTK_WINDOW(b.window), GTK_FILE_CHOOSER_ACTION_SAVE,
            "Cancel", GTK_RESPONSE_CANCEL, "Save", GTK_RESPONSE_ACCEPT, nullptr);
        gtk_file_chooser_set_current_name(GTK_FILE_CHOOSER(chooser), suggested_name.c_str());
        gint choice = gtk_dialog_run(GTK_DIALOG(chooser));
        std::string path;
        if (choice == GTK_RESPONSE_ACCEPT) {
            gchar* chosen = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(chooser));
            if (chosen) path = chosen;
            g_free(chosen);
        }
        gtk_widget_destroy(chooser);
        return path;
    };
    session_cb.download_started = [&b](std::uint64_t id, const std::string& path) {
        DownloadRow row;
        row.id = id;
        row.path = path;
        size_t slash = path.rfind('/');
        row.name = slash == std::string::npos ? path : path.substr(slash + 1);
        b.downloads[id] = row;
        flash_status(b, "downloading " + row.name);
        refresh_downloads_window(b);
    };
    session_cb.download_progress = [&b](std::uint64_t id, double fraction) {
        auto it = b.downloads.find(id);
        if (it == b.downloads.end()) return;
        it->second.progress = fraction;
        refresh_status(b);
        refresh_downloads_window(b);
    };
    session_cb.download_finished = [&b](std::uint64_t id) {
        auto it = b.downloads.find(id);
        if (it != b.downloads.end()) {
            it->second.progress = 1.0;
            it->second.running = false;
            flash_status(b, "download finished: " + it->second.name);
        }
        refresh_downloads_window(b);
    };
    session_cb.download_failed = [&b](std::uint64_t id) {
        auto it = b.downloads.find(id);
        if (it != b.downloads.end()) {
            it->second.running = false;
            it->second.failed = true;
            flash_status(b, "download failed: " + it->second.name);
        }
        refresh_downloads_window(b);
    };
    b.session = std::make_unique<mustela::ViewSession>(config, session_cb);

    GtkWidget* window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    b.window = window;
    gtk_window_set_title(GTK_WINDOW(window), "Kiwi");
    gtk_window_set_default_size(GTK_WINDOW(window), b.settings.window_width, b.settings.window_height);
    g_signal_connect(window, "destroy", G_CALLBACK(on_destroy), nullptr);
    g_signal_connect(window, "key-press-event", G_CALLBACK(on_key_press), nullptr);

    GtkWidget* toolbar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 2);
    gtk_container_set_border_width(GTK_CONTAINER(toolbar), 2);

    b.back_btn = gtk_button_new_from_icon_name("go-previous-symbolic", GTK_ICON_SIZE_BUTTON);
    b.forward_btn = gtk_button_new_from_icon_name("go-next-symbolic", GTK_ICON_SIZE_BUTTON);
    b.reload_btn = gtk_button_new_from_icon_name("view-refresh-symbolic", GTK_ICON_SIZE_BUTTON);
    b.private_toggle = gtk_toggle_button_new_with_label("Private");
    gtk_widget_set_tooltip_text(b.private_toggle, "New tabs open in private mode");
    b.privacy_btn = gtk_button_new();
    GtkWidget* privacy_dot = gtk_label_new("\xE2\x97\x8F");
    gtk_container_add(GTK_CONTAINER(b.privacy_btn), privacy_dot);
    gtk_button_set_relief(GTK_BUTTON(b.privacy_btn), GTK_RELIEF_NONE);
    gtk_widget_set_focus_on_click(b.privacy_btn, FALSE);
    GtkStyleContext* dot_ctx = gtk_widget_get_style_context(privacy_dot);
    gtk_style_context_add_class(dot_ctx, "kiwi-dot-idle");
    gtk_widget_set_tooltip_text(b.privacy_btn, "Privacy Center — site overrides and stored data");
    b.url_entry = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(b.url_entry), "Search or enter address");
    gtk_entry_set_icon_from_icon_name(GTK_ENTRY(b.url_entry), GTK_ENTRY_ICON_SECONDARY,
        "non-starred-symbolic");
    gtk_entry_set_icon_tooltip_text(GTK_ENTRY(b.url_entry), GTK_ENTRY_ICON_SECONDARY,
        "Bookmark this page (Ctrl+D)");
    g_signal_connect(b.url_entry, "icon-press", G_CALLBACK(on_bookmark_icon_pressed), nullptr);
    b.status_label = gtk_label_new("");
    gtk_label_set_xalign(GTK_LABEL(b.status_label), 1.0f);
    gtk_style_context_add_class(gtk_widget_get_style_context(b.status_label), "kiwi-status");
    gtk_widget_set_margin_start(b.status_label, 6);
    gtk_widget_set_margin_end(b.status_label, 6);
    gtk_widget_set_valign(b.status_label, GTK_ALIGN_CENTER);
    gtk_widget_set_tooltip_text(b.status_label,
        "Cookie policy, tracking prevention and fingerprint budget. "
        "Ctrl+F find, Ctrl+S save page, F11 fullscreen, Ctrl+D bookmark, "
        "Ctrl+Shift+O bookmarks, Ctrl+H history, Ctrl+J downloads");
    GtkWidget* new_tab_btn = gtk_button_new_from_icon_name("tab-new-symbolic", GTK_ICON_SIZE_BUTTON);
    b.downloads_btn = gtk_button_new_from_icon_name("document-save-symbolic", GTK_ICON_SIZE_BUTTON);
    gtk_button_set_relief(GTK_BUTTON(b.downloads_btn), GTK_RELIEF_NONE);
    gtk_widget_set_focus_on_click(b.downloads_btn, FALSE);
    gtk_widget_set_tooltip_text(b.downloads_btn, "Downloads (Ctrl+J)");
    b.settings_btn = gtk_button_new_from_icon_name("emblem-system-symbolic", GTK_ICON_SIZE_BUTTON);
    gtk_button_set_relief(GTK_BUTTON(b.settings_btn), GTK_RELIEF_NONE);
    gtk_widget_set_focus_on_click(b.settings_btn, FALSE);
    gtk_widget_set_tooltip_text(b.settings_btn, "Settings — search engines and profiles (Ctrl+,)");

    b.find_bar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
    gtk_widget_set_no_show_all(b.find_bar, TRUE);
    gtk_container_set_border_width(GTK_CONTAINER(b.find_bar), 2);
    gtk_style_context_add_class(gtk_widget_get_style_context(b.find_bar), "kiwi-findbar");
    GtkWidget* find_icon = gtk_image_new_from_icon_name("edit-find-symbolic", GTK_ICON_SIZE_BUTTON);
    gtk_widget_set_margin_start(find_icon, 4);
    b.find_entry = gtk_search_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(b.find_entry), "Find in page");
    gtk_widget_set_size_request(b.find_entry, 260, -1);
    b.find_hint = gtk_label_new("");
    gtk_style_context_add_class(gtk_widget_get_style_context(b.find_hint), "kiwi-secondary");
    GtkWidget* find_prev_btn = gtk_button_new_from_icon_name("go-up-symbolic", GTK_ICON_SIZE_BUTTON);
    gtk_widget_set_tooltip_text(find_prev_btn, "Previous match (Shift+F3)");
    GtkWidget* find_next_btn = gtk_button_new_from_icon_name("go-down-symbolic", GTK_ICON_SIZE_BUTTON);
    gtk_widget_set_tooltip_text(find_next_btn, "Next match (F3 or Enter)");
    GtkWidget* find_close_btn = gtk_button_new_from_icon_name("window-close-symbolic", GTK_ICON_SIZE_BUTTON);
    gtk_widget_set_tooltip_text(find_close_btn, "Close find bar (Esc)");
    GtkWidget* find_buttons[] = {find_prev_btn, find_next_btn, find_close_btn};
    for (GtkWidget* btn : find_buttons) {
        gtk_button_set_relief(GTK_BUTTON(btn), GTK_RELIEF_NONE);
        gtk_widget_set_focus_on_click(btn, FALSE);
    }
    gtk_box_pack_start(GTK_BOX(b.find_bar), find_icon, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(b.find_bar), b.find_entry, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(b.find_bar), b.find_hint, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(b.find_bar), find_prev_btn, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(b.find_bar), find_next_btn, FALSE, FALSE, 0);
    gtk_box_pack_end(GTK_BOX(b.find_bar), find_close_btn, FALSE, FALSE, 0);

    GtkWidget* toolbar_buttons[] = {b.back_btn, b.forward_btn, b.reload_btn, new_tab_btn};
    for (GtkWidget* btn : toolbar_buttons) {
        gtk_button_set_relief(GTK_BUTTON(btn), GTK_RELIEF_NONE);
        gtk_widget_set_focus_on_click(btn, FALSE);
    }
    gtk_button_set_relief(GTK_BUTTON(b.private_toggle), GTK_RELIEF_NONE);
    gtk_widget_set_focus_on_click(b.private_toggle, FALSE);
    gtk_widget_set_hexpand(b.url_entry, TRUE);

    gtk_box_pack_start(GTK_BOX(toolbar), b.back_btn, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(toolbar), b.forward_btn, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(toolbar), b.reload_btn, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(toolbar), b.private_toggle, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(toolbar), b.privacy_btn, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(toolbar), b.url_entry, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(toolbar), b.downloads_btn, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(toolbar), b.settings_btn, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(toolbar), new_tab_btn, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(toolbar), b.status_label, FALSE, FALSE, 0);

    b.notebook = gtk_notebook_new();
    gtk_notebook_set_scrollable(GTK_NOTEBOOK(b.notebook), TRUE);
    gtk_notebook_set_show_tabs(GTK_NOTEBOOK(b.notebook), FALSE);
    wire_notebook_drops(b.notebook);

    GtkWidget* layout = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_box_pack_start(GTK_BOX(layout), toolbar, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(layout), b.find_bar, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(layout), b.notebook, TRUE, TRUE, 0);
    gtk_container_add(GTK_CONTAINER(window), layout);

    g_signal_connect(b.back_btn, "clicked", G_CALLBACK(on_go_back), nullptr);
    g_signal_connect(b.forward_btn, "clicked", G_CALLBACK(on_go_forward), nullptr);
    g_signal_connect(b.reload_btn, "clicked", G_CALLBACK(on_reload), nullptr);
    g_signal_connect(b.private_toggle, "toggled", G_CALLBACK(on_private_toggled), nullptr);
    g_signal_connect(b.privacy_btn, "clicked", G_CALLBACK(on_privacy_clicked), nullptr);
    g_signal_connect(new_tab_btn, "clicked", G_CALLBACK(on_new_tab), nullptr);
    g_signal_connect(b.downloads_btn, "clicked", G_CALLBACK(on_downloads_clicked), nullptr);
    g_signal_connect(b.settings_btn, "clicked", G_CALLBACK(on_settings_clicked), nullptr);
    g_signal_connect(b.find_entry, "search-changed", G_CALLBACK(on_find_changed), nullptr);
    g_signal_connect(b.find_entry, "activate", G_CALLBACK(on_find_activate), nullptr);
    g_signal_connect(b.find_entry, "stop-search", G_CALLBACK(on_find_stop), nullptr);
    g_signal_connect(find_next_btn, "clicked", G_CALLBACK(on_find_next_clicked), nullptr);
    g_signal_connect(find_prev_btn, "clicked", G_CALLBACK(on_find_previous_clicked), nullptr);
    g_signal_connect(find_close_btn, "clicked", G_CALLBACK(on_find_stop), nullptr);
    g_signal_connect(b.url_entry, "activate", G_CALLBACK(on_url_activate), nullptr);
    g_signal_connect(b.notebook, "switch-page", G_CALLBACK(on_switch_page), nullptr);
    g_signal_connect(b.notebook, "page-removed", G_CALLBACK(on_page_removed), nullptr);
    g_signal_connect(b.notebook, "page-reordered", G_CALLBACK(on_page_reordered), nullptr);

    if (b.start_private) {
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(b.private_toggle), TRUE);
    }
    open_tab(b, startup_url && *startup_url ? startup_url : b.settings.homepage, b.start_private);

    kiwi::install_shortcuts(GTK_WINDOW(window), [&b](guint kv, GdkModifierType m) {
        return kiwi_shortcut(b, kv, m);
    });

    gtk_widget_show_all(window);
}
