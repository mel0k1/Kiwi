#include "browser.hpp"

#include <gtk/gtk.h>

#include <string>

#include "mustela/mustela.hpp"
#include "mustela/view.hpp"

namespace {

Browser* ctx_browser = nullptr;

KiwiTab* ctx_tab() {
    if (!ctx_browser) return nullptr;
    return active_tab(*ctx_browser);
}

void menu_item_set_uri(GtkWidget* item, const std::string& uri) {
    g_object_set_data_full(G_OBJECT(item), "kiwi-uri", g_strdup(uri.c_str()), g_free);
}

std::string menu_item_get_uri(GtkMenuItem* item) {
    const gchar* uri = static_cast<const gchar*>(g_object_get_data(G_OBJECT(item), "kiwi-uri"));
    return uri ? std::string(uri) : std::string();
}

void on_ctx_back(GtkMenuItem*, gpointer) {
    KiwiTab* tab = ctx_tab();
    if (tab) tab->view->go_back();
}

void on_ctx_forward(GtkMenuItem*, gpointer) {
    KiwiTab* tab = ctx_tab();
    if (tab) tab->view->go_forward();
}

void on_ctx_reload(GtkMenuItem*, gpointer) {
    KiwiTab* tab = ctx_tab();
    if (tab) tab->view->reload();
}

void on_ctx_open_link(GtkMenuItem* item, gpointer) {
    KiwiTab* tab = ctx_tab();
    if (!tab || !ctx_browser) return;
    std::string uri = menu_item_get_uri(item);
    if (!uri.empty()) open_tab(*ctx_browser, uri, tab->private_tab);
}

void on_ctx_copy_uri(GtkMenuItem* item, gpointer) {
    std::string uri = menu_item_get_uri(item);
    if (uri.empty()) return;
    gtk_clipboard_set_text(gtk_clipboard_get(GDK_SELECTION_CLIPBOARD), uri.c_str(), -1);
}

void on_ctx_copy(GtkMenuItem*, gpointer) {
    KiwiTab* tab = ctx_tab();
    if (tab) tab->view->execute_edit_command(mustela::EditCommand::Copy);
}

void on_ctx_cut(GtkMenuItem*, gpointer) {
    KiwiTab* tab = ctx_tab();
    if (tab) tab->view->execute_edit_command(mustela::EditCommand::Cut);
}

void on_ctx_paste(GtkMenuItem*, gpointer) {
    KiwiTab* tab = ctx_tab();
    if (tab) tab->view->execute_edit_command(mustela::EditCommand::Paste);
}

void on_ctx_select_all(GtkMenuItem*, gpointer) {
    KiwiTab* tab = ctx_tab();
    if (tab) tab->view->execute_edit_command(mustela::EditCommand::SelectAll);
}

void on_ctx_search_with(GtkMenuItem*, gpointer) {
    KiwiTab* tab = ctx_tab();
    if (!tab || !ctx_browser) return;
    tab->view->selected_text([](const std::string& text) {
        if (!ctx_browser || text.empty()) return;
        Browser& b = *ctx_browser;
        mustela::SearchEngine engine = mustela::default_search_engine(b.settings);
        std::string url = mustela::fill_search_template(engine, text);
        KiwiTab* current = active_tab(b);
        if (!url.empty()) open_tab(b, url, current ? current->private_tab : false);
    });
}

void on_ctx_inspect(GtkMenuItem*, gpointer) {
    KiwiTab* tab = ctx_tab();
    if (tab) tab->view->show_devtools();
}

GtkWidget* ctx_add(GtkWidget* menu, const char* label, GCallback handler, bool sensitive) {
    GtkWidget* item = gtk_menu_item_new_with_label(label);
    gtk_widget_set_sensitive(item, sensitive ? TRUE : FALSE);
    if (handler) g_signal_connect(item, "activate", handler, nullptr);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);
    return item;
}

void ctx_separator(GtkWidget* menu) {
    GtkWidget* sep = gtk_separator_menu_item_new();
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), sep);
}

}

void open_context_menu(Browser& b, const mustela::ContextMenuInfo& info) {
    ctx_browser = &b;
    GtkWidget* menu = gtk_menu_new();
    gtk_style_context_add_class(gtk_widget_get_style_context(menu), "kiwi-menu");
    KiwiTab* tab = active_tab(b);
    bool has_tab = tab != nullptr;
    ctx_add(menu, "Back", G_CALLBACK(on_ctx_back), has_tab && tab->view->can_go_back());
    ctx_add(menu, "Forward", G_CALLBACK(on_ctx_forward), has_tab && tab->view->can_go_forward());
    ctx_add(menu, "Reload", G_CALLBACK(on_ctx_reload), has_tab && !info.page_uri.empty());
    bool extras = info.link || info.image || info.selection || info.editable;
    if (extras) ctx_separator(menu);
    if (info.link && !info.link_uri.empty()) {
        GtkWidget* open_item = ctx_add(menu, "Open link in new tab",
            G_CALLBACK(on_ctx_open_link), has_tab);
        menu_item_set_uri(open_item, info.link_uri);
        GtkWidget* copy_item = ctx_add(menu, "Copy link address", G_CALLBACK(on_ctx_copy_uri), true);
        menu_item_set_uri(copy_item, info.link_uri);
    }
    if (info.image && !info.image_uri.empty()) {
        GtkWidget* open_item = ctx_add(menu, "Open image in new tab",
            G_CALLBACK(on_ctx_open_link), has_tab);
        menu_item_set_uri(open_item, info.image_uri);
        GtkWidget* copy_item = ctx_add(menu, "Copy image address", G_CALLBACK(on_ctx_copy_uri), true);
        menu_item_set_uri(copy_item, info.image_uri);
    }
    if (info.selection) {
        ctx_add(menu, "Copy", G_CALLBACK(on_ctx_copy), true);
        mustela::SearchEngine engine = mustela::default_search_engine(b.settings);
        std::string label = engine.name.empty() ? std::string("Search the web")
            : "Search with " + engine.name;
        ctx_add(menu, label.c_str(), G_CALLBACK(on_ctx_search_with), true);
    }
    if (info.editable) {
        ctx_add(menu, "Cut", G_CALLBACK(on_ctx_cut), true);
        ctx_add(menu, "Paste", G_CALLBACK(on_ctx_paste), true);
        ctx_add(menu, "Select all", G_CALLBACK(on_ctx_select_all), true);
    }
    ctx_separator(menu);
    ctx_add(menu, "Inspect element", G_CALLBACK(on_ctx_inspect), has_tab);
    gtk_widget_show_all(menu);
    gtk_menu_popup_at_pointer(GTK_MENU(menu), nullptr);
}
