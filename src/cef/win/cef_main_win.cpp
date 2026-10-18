#include "../cef_app.hpp"
#include "../cef_runtime.hpp"
#include "cef_shell_win.hpp"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include "include/cef_app.h"

#include "diagnostics.hpp"

namespace kiwi {

namespace {

void apply_cef_settings(CefSettings& settings) {
    Runtime* rt = runtime();
    settings.no_sandbox = true;
    settings.multi_threaded_message_loop = false;
    settings.windowless_rendering_enabled = false;
    settings.log_severity = LOGSEVERITY_WARNING;
    if (!rt) return;
    CefString(&settings.root_cache_path) = rt->cache_root;
    CefString(&settings.cache_path) = rt->cache_root + "/global";
    CefString(&settings.log_file) = rt->config_dir + "/cef.log";
}

}  // namespace

}  // namespace kiwi

int APIENTRY wWinMain(HINSTANCE instance, HINSTANCE, LPWSTR, int) {
    (void)instance;
    kiwi::install_crash_handler();

    CefMainArgs args(GetModuleHandleW(nullptr));
    CefRefPtr<kiwi::KiwiCefApp> app(new kiwi::KiwiCefApp());

    int exit_code = CefExecuteProcess(args, app, nullptr);
    if (exit_code >= 0) return exit_code;

    kiwi::runtime_init();

    CefSettings settings;
    kiwi::apply_cef_settings(settings);
    CefInitialize(args, settings, app, nullptr);

    int result = kiwi::cef_shell_main();

    kiwi::Runtime* rt = kiwi::runtime();
    if (rt) mustela::save_settings(rt->settings_path, rt->settings);

    CefShutdown();
    return result;
}
