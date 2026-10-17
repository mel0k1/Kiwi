#pragma once

#include <gtk/gtk.h>

#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "mustela/mustela.hpp"
#include "mustela/view.hpp"

struct KiwiTab {
    std::unique_ptr<mustela::View> view;
    GtkWidget* label = nullptr;
    GtkWidget* icon = nullptr;
    GtkWidget* title_label = nullptr;
    std::string title;
    std::string uri;
    bool private_tab = false;
};

struct DownloadRow {
    std::uint64_t id = 0;
    std::string name;
    std::string path;
    double progress = 0;
    bool running = true;
    bool failed = false;
};

struct Browser {
    mustela::Settings settings;
    mustela::PrivacyPolicy privacy;
    std::unique_ptr<mustela::SiteRules> sites;
    std::unique_ptr<mustela::Profile> profile;
    std::unique_ptr<mustela::HistoryStore> history;
    std::unique_ptr<mustela::BookmarkStore> bookmarks;
    std::unique_ptr<mustela::ViewSession> session;
    GApplication* gapp = nullptr;
    guint notification_seq = 0;
    GtkWidget* window = nullptr;
    GtkWidget* notebook = nullptr;
    GtkWidget* url_entry = nullptr;
    GtkWidget* back_btn = nullptr;
    GtkWidget* forward_btn = nullptr;
    GtkWidget* reload_btn = nullptr;
    GtkWidget* private_toggle = nullptr;
    GtkWidget* privacy_btn = nullptr;
    GtkWidget* status_label = nullptr;
    GtkWidget* downloads_btn = nullptr;
    GtkWidget* settings_btn = nullptr;
    GtkWidget* find_bar = nullptr;
    GtkWidget* find_entry = nullptr;
    GtkWidget* find_hint = nullptr;
    std::string find_query;
    GtkWidget* history_window = nullptr;
    GtkWidget* downloads_window = nullptr;
    GtkWidget* privacy_window = nullptr;
    GtkWidget* bookmarks_window = nullptr;
    GtkWidget* settings_window = nullptr;
    GtkWidget* downloads_tree = nullptr;
    GtkListStore* downloads_store = nullptr;
    GtkListStore* bookmarks_store = nullptr;
    std::string hover_link;
    std::vector<std::unique_ptr<KiwiTab>> tabs;
    std::map<std::uint64_t, DownloadRow> downloads;
    bool start_private = false;
};

void build_browser_ui(Browser& b, const char* startup_url);
KiwiTab* active_tab(Browser& b);
KiwiTab* open_tab(Browser& b, const std::string& url, bool force_private = false);
void close_tab(Browser& b, KiwiTab* tab);
void navigate_active(Browser& b, const std::string& entry_text);
void refresh_chrome(Browser& b);
void flash_status(Browser& b, const std::string& text);

void open_history_window(Browser& b);
void open_downloads_window(Browser& b);
void refresh_downloads_window(Browser& b);
void open_privacy_window(Browser& b, const std::string& focus_host = std::string());
void update_privacy_indicator(Browser& b);
void open_bookmarks_window(Browser& b);
void refresh_bookmarks_window(Browser& b);
void update_bookmark_star(Browser& b);
void toggle_bookmark(Browser& b);
void open_settings_window(Browser& b);
void toggle_find_bar(Browser& b, bool show);
void toggle_devtools(Browser& b);
void open_context_menu(Browser& b, const mustela::ContextMenuInfo& info);
