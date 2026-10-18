#include "cef_runtime.hpp"

#include "start_page.hpp"

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

#include <cstdlib>
#include <filesystem>
#include <system_error>

namespace kiwi {

namespace {

Runtime* g_runtime = nullptr;

#ifdef _WIN32

std::string utf8_from_wide(const wchar_t* wide, int size) {
    if (size <= 0) return std::string();
    int utf8 = WideCharToMultiByte(CP_UTF8, 0, wide, size, nullptr, 0, nullptr, nullptr);
    std::string out(static_cast<size_t>(utf8), '\0');
    if (utf8 > 0) {
        WideCharToMultiByte(CP_UTF8, 0, wide, size, out.data(), utf8, nullptr, nullptr);
        while (!out.empty() && out.back() == '\0') out.pop_back();
    }
    return out;
}

std::string env_utf8(const wchar_t* name) {
    wchar_t buffer[MAX_PATH * 2];
    DWORD size = GetEnvironmentVariableW(name, buffer, MAX_PATH * 2);
    if (size == 0 || size > MAX_PATH * 2) return std::string();
    return utf8_from_wide(buffer, static_cast<int>(size));
}

#else

std::string env_utf8(const char* name) {
    const char* value = std::getenv(name);
    return value ? std::string(value) : std::string();
}

#endif

void ensure_dir(const std::string& path) {
    std::error_code ec;
    std::filesystem::create_directories(path, ec);
}

std::string config_directory() {
#ifdef _WIN32
    std::string base = env_utf8(L"LOCALAPPDATA");
    if (base.empty()) {
        base = env_utf8(L"USERPROFILE");
        if (!base.empty()) base += "\\AppData\\Local";
    }
    if (base.empty()) base = ".";
    return base + "\\kiwi";
#else
    std::string base = env_utf8("XDG_CONFIG_HOME");
    if (base.empty()) {
        base = env_utf8("HOME");
        if (!base.empty()) base += "/.config";
    }
    if (base.empty()) base = ".";
    return base + "/kiwi";
#endif
}

std::string default_downloads_dir() {
#ifdef _WIN32
    std::string base = env_utf8(L"USERPROFILE");
    if (!base.empty()) return base + "\\Downloads";
    return ".";
#else
    std::string base = env_utf8("HOME");
    return (base.empty() ? std::string(".") : base) + "/Downloads";
#endif
}

}  // namespace

void runtime_init() {
    Runtime* rt = new Runtime();
    rt->config_dir = config_directory();
    ensure_dir(rt->config_dir);
    rt->settings_path = rt->config_dir + "/settings.toml";
    rt->settings = mustela::load_settings(rt->settings_path);

    rt->policy = mustela::privacy_from(rt->settings);
    rt->policy.private_tabs_available = rt->settings.private_tabs_enabled;
    rt->blocklist = mustela::default_blocklist(
        rt->policy.blocklist ? rt->settings.blocklist_file : std::string());

    rt->profile_root = rt->config_dir + "/profiles/" + rt->settings.shell_profile;
    mustela::Profile profile(rt->profile_root);
    profile.ensure_dirs();

    rt->cache_root = rt->config_dir + "/cef";
    ensure_dir(rt->cache_root);

    rt->downloads_dir = rt->settings.downloads_dir.empty()
        ? default_downloads_dir()
        : rt->settings.downloads_dir;

    rt->bookmarks = std::make_unique<mustela::BookmarkStore>(profile.paths().bookmarks);
    rt->history = std::make_unique<mustela::HistoryStore>(profile.paths().history);

    // Same store the GTK build uses: <config>/sites.toml. Create an empty
    // file on first run so the per-site editor and the render-process loader
    // always see a valid store.
    const std::string sites_path = rt->config_dir + "/sites.toml";
    if (!std::filesystem::exists(sites_path)) {
        mustela::SiteRules initial(sites_path);
        initial.save();
    }
    rt->site_rules = std::make_unique<mustela::SiteRules>(sites_path);
    rt->site_rules->load();
    rt->site_rules_path = sites_path;

    g_runtime = rt;
}

Runtime* runtime() {
    return g_runtime;
}

std::string Runtime::start_page_uri(bool private_mode) {
    return kiwi::write_start_page(profile_root, settings, bookmarks->entries(), private_mode);
}

}
