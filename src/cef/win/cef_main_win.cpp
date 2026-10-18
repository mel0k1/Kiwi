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
#include "include/cef_version_info.h"

#include "diagnostics.hpp"

#include <exception>
#include <string>

namespace kiwi {

namespace {

// The shell is a Win32 window: PMv2 awareness is set at runtime so the
// process is DPI-aware regardless of which manifest bootstrap.exe carries.
void enable_per_monitor_dpi() {
    using SetDpiCtxFn = BOOL(WINAPI*)(DPI_AWARENESS_CONTEXT);
    HMODULE user32 = GetModuleHandleW(L"user32.dll");
    if (!user32) return;
    FARPROC proc = GetProcAddress(user32, "SetProcessDpiAwarenessContext");
    if (!proc) return;
    auto fn = reinterpret_cast<SetDpiCtxFn>(
        reinterpret_cast<std::uintptr_t>(proc));
    fn(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
}

void apply_cef_settings(CefSettings& settings, bool sandboxed) {
    Runtime* rt = runtime();
    // With the bootstrap loader the sandbox context is prepared by the
    // executable and must stay enabled; a direct DLL load has no sandbox
    // context, so that path keeps the old no-sandbox fallback.
    settings.no_sandbox = sandboxed ? false : true;
    settings.multi_threaded_message_loop = false;
    settings.windowless_rendering_enabled = false;
    settings.log_severity = LOGSEVERITY_WARNING;
    if (!rt) return;
    CefString(&settings.root_cache_path) = rt->cache_root;
    CefString(&settings.cache_path) = rt->cache_root + "/global";
    CefString(&settings.log_file) = rt->config_dir + "/cef.log";
}

bool cmdline_has_self_test(LPWSTR lpCmdLine) {
    if (!lpCmdLine || !*lpCmdLine) return false;
    // Match the flag anywhere in the command line; bootstrap.exe passes the
    // original arguments through unmodified.
    return std::wcsstr(lpCmdLine, L"--self-test") != nullptr;
}

}  // namespace

}  // namespace kiwi

extern "C" __declspec(dllexport) int RunWinMain(HINSTANCE hInstance,
                                                LPWSTR lpCmdLine,
                                                int nCmdShow,
                                                void* sandbox_info,
                                                cef_version_info_t* version_info) {
    (void)nCmdShow;
    (void)version_info;  // bootstrap installer metadata; informational

    kiwi::install_crash_handler();
    kiwi::enable_per_monitor_dpi();

    const bool self_test = kiwi::cmdline_has_self_test(lpCmdLine);
    kiwi::trace_stage(std::string("RunWinMain enter: Kiwi " KIWI_VERSION
        ", sandbox_context=") + (sandbox_info ? "yes" : "no")
        + ", self_test=" + (self_test ? "yes" : "no"));

    CefMainArgs args(hInstance);
    CefRefPtr<kiwi::KiwiCefApp> app(new kiwi::KiwiCefApp());

    // Subprocesses (render/GPU/network) re-enter here via kiwi.exe; forward
    // the sandbox context so they run sandboxed.
    int exit_code = CefExecuteProcess(args, app, sandbox_info);
    if (exit_code >= 0) {
        kiwi::trace_stage("subprocess exit: " + std::to_string(exit_code));
        return exit_code;
    }

    try {
        kiwi::runtime_init();
        kiwi::trace_stage("runtime_init ok");
    } catch (const std::exception& e) {
        kiwi::fatal_dialog("Kiwi — profile error",
            std::string("Kiwi failed to read its profile.\n\n") + e.what()
            + "\n\nDiagnostics: %LocalAppData%\\kiwi\\startup.log");
        return 3;
    } catch (...) {
        kiwi::fatal_dialog("Kiwi — profile error",
            "Kiwi failed to read its profile (unknown error).\n\n"
            "Diagnostics: %LocalAppData%\\kiwi\\startup.log");
        return 3;
    }

    CefSettings settings;
    kiwi::apply_cef_settings(settings, sandbox_info != nullptr);
    kiwi::trace_stage("CefInitialize begin (no_sandbox="
        + std::to_string(settings.no_sandbox ? 1 : 0) + ")");

    if (!CefInitialize(args, settings, app, sandbox_info)) {
        kiwi::fatal_dialog("Kiwi — engine error",
            "CefInitialize failed. Another Kiwi instance may be running, "
            "or the CEF runtime files are missing.\n\n"
            "Diagnostics: %LocalAppData%\\kiwi\\startup.log and cef.log");
        return 4;
    }
    kiwi::trace_stage("CefInitialize ok");

    int result = kiwi::cef_shell_main(self_test);
    kiwi::trace_stage("shell done: " + std::to_string(result));

    kiwi::Runtime* rt = kiwi::runtime();
    if (rt) mustela::save_settings(rt->settings_path, rt->settings);

    CefShutdown();
    kiwi::trace_stage("clean exit");
    return result;
}
