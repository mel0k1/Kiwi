#include <gtk/gtk.h>
#include <glib/gstdio.h>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

#include <cstring>
#include <memory>
#include <string>

#include "mustela/mustela.hpp"
#include "mustela/view.hpp"

#include "browser.hpp"
#include "diagnostics.hpp"
#include "theme.hpp"

#ifdef _WIN32
// Installed and portable Windows runs ship a relocatable gdk-pixbuf loader
// cache next to the executable. Point the loader at it before GTK init so a
// Start-menu shortcut can launch kiwi.exe directly, without the .cmd wrapper.
static void apply_windows_pixbuf_path() {
    wchar_t exe[MAX_PATH];
    if (GetModuleFileNameW(nullptr, exe, MAX_PATH) == 0) return;
    std::wstring dir(exe);
    size_t slash = dir.find_last_of(L"\\/");
    if (slash == std::wstring::npos) return;
    dir.resize(slash);
    std::wstring cache = dir + L"\\lib\\gdk-pixbuf-2.0\\2.10.0\\loaders.cache";
    if (GetFileAttributesW(cache.c_str()) == INVALID_FILE_ATTRIBUTES) return;
    SetEnvironmentVariableW(L"GDK_PIXBUF_MODULE_FILE", cache.c_str());
}
#endif

int main(int argc, char** argv) {
#ifdef _WIN32
    apply_windows_pixbuf_path();
#endif
    kiwi::install_crash_handler();
    gtk_init(&argc, &argv);
    kiwi::install_gtk_log_file();
    kiwi::apply_theme();
    kiwi::apply_app_icon();

    const char* url = nullptr;
    bool start_private = false;
    bool self_test = false;
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--private") == 0) start_private = true;
        else if (std::strcmp(argv[i], "--self-test") == 0) self_test = true;
        else url = argv[i];
    }

    gchar* config_dir = g_build_filename(g_get_user_config_dir(), "kiwi", nullptr);
    g_mkdir_with_parents(config_dir, 0700);
    gchar* settings_path = g_build_filename(config_dir, "settings.toml", nullptr);
    mustela::SettingsReport report;
    mustela::Settings settings = mustela::load_settings(settings_path, report);
    if (!g_file_test(settings_path, G_FILE_TEST_EXISTS)) {
        mustela::save_settings(settings_path, settings);
    }
    mustela::PrivacyPolicy privacy = mustela::privacy_from(settings);

    gchar* sites_path = g_build_filename(config_dir, "sites.toml", nullptr);
    if (!g_file_test(sites_path, G_FILE_TEST_EXISTS)) {
        mustela::SiteRules initial(sites_path);
        initial.save();
    }
    auto sites = std::make_unique<mustela::SiteRules>(sites_path);
    sites->load();
    g_free(sites_path);

    gchar* profiles_path = g_build_filename(config_dir, "profiles.toml", nullptr);
    if (!g_file_test(profiles_path, G_FILE_TEST_EXISTS)) {
        mustela::save_profile_registry(profiles_path, mustela::default_profile_registry());
    }
    mustela::ProfileRegistry registry = mustela::load_profile_registry(profiles_path);
    g_free(profiles_path);
    const mustela::ProfileEntry* entry = registry.find(settings.shell_profile);
    if (!entry) entry = registry.find(registry.default_id);
    if (!entry && !registry.entries.empty()) entry = &registry.entries.front();
    mustela::ProfileEntry fallback;
    if (!entry) {
        fallback.id = "default";
        fallback.root = "profiles/default";
        entry = &fallback;
    }
    gchar* data_base = g_build_filename(g_get_user_data_dir(), "kiwi", nullptr);
    std::string profile_root = mustela::resolve_profile_root(*entry, data_base);
    g_free(data_base);
    g_free(config_dir);
    g_free(settings_path);

    Browser browser;
    browser.gapp = g_application_new("io.github.mel0k1.Kiwi", G_APPLICATION_NON_UNIQUE);
    GError* gapp_error = nullptr;
    if (g_application_register(browser.gapp, nullptr, &gapp_error)) {
        g_application_hold(browser.gapp);
    } else {
        // No session bus (headless or restricted sandbox): notifications drop quietly.
        if (gapp_error) g_error_free(gapp_error);
        g_object_unref(browser.gapp);
        browser.gapp = nullptr;
    }
    browser.settings = settings;
    browser.privacy = privacy;
    browser.sites = std::move(sites);
    browser.profile = std::make_unique<mustela::Profile>(profile_root, browser.privacy);
    browser.profile->ensure_dirs();
    if (browser.privacy.history_enabled) {
        browser.history = std::make_unique<mustela::HistoryStore>(browser.profile->paths().history);
    }
    browser.bookmarks = std::make_unique<mustela::BookmarkStore>(browser.profile->paths().bookmarks);
    browser.start_private = start_private
        || browser.privacy.default_private == mustela::TabPrivacy::Private;

    build_browser_ui(browser, url);
    if (self_test) run_self_test(browser);
    gtk_main();
    return kiwi_self_test_exit_code();
}
