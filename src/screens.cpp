#include "browser.hpp"

#include <glib/gstdio.h>

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
