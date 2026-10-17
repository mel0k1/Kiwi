#include <gtk/gtk.h>
#include <glib/gstdio.h>

#include <cstring>
#include <memory>
#include <string>

#include "mustela/mustela.hpp"
#include "mustela/view.hpp"

#include "browser.hpp"

int main(int argc, char** argv) {
    gtk_init(&argc, &argv);

    const char* url = nullptr;
    bool start_private = false;
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--private") == 0) start_private = true;
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
    browser.settings = settings;
    browser.privacy = privacy;
    browser.sites = std::move(sites);
    browser.profile = std::make_unique<mustela::Profile>(profile_root, browser.privacy);
    browser.profile->ensure_dirs();
    if (browser.privacy.history_enabled) {
        browser.history = std::make_unique<mustela::HistoryStore>(browser.profile->paths().history);
    }
    browser.start_private = start_private
        || browser.privacy.default_private == mustela::TabPrivacy::Private;

    build_browser_ui(browser, url);
    gtk_main();
    return 0;
}
