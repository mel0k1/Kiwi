#include "cef_app.hpp"

#include "cef_runtime.hpp"

#include "fingerprint_scripts.hpp"

#include <string>

namespace kiwi {

namespace {

std::string feature_param_escape(const std::string& raw) {
    static const char hex[] = "0123456789ABCDEF";
    std::string out;
    for (unsigned char c : raw) {
        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9')
            || c == '-' || c == '_' || c == '.') {
            out += static_cast<char>(c);
        } else {
            out += '%';
            out += hex[c >> 4];
            out += hex[c & 0x0F];
        }
    }
    return out;
}

std::string switch_value(CefRefPtr<CefCommandLine> command_line, const char* name) {
    return command_line->GetSwitchValue(name).ToString();
}

}  // namespace

void KiwiCefApp::OnBeforeCommandLineProcessing(const CefString& process_type,
                                               CefRefPtr<CefCommandLine> command_line) {
    if (!process_type.empty()) return;
    Runtime* rt = runtime();
    if (!rt) return;
    const mustela::Settings& settings = rt->settings;

    command_line->AppendSwitchWithValue("disable-features",
        "Translate,MediaRouter,OptimizationHints");

    if (settings.block_autoplay) {
        command_line->AppendSwitchWithValue("autoplay-policy", "user-gesture-required");
    }

    if (!settings.dns_over_https.empty()) {
        command_line->AppendSwitchWithValue("enable-features",
            std::string("DnsOverHttps<dns_over_https_mode/secure/dns_over_https_templates/")
            + feature_param_escape(settings.dns_over_https));
    }

    command_line->AppendSwitchWithValue("kiwi-fp-webgl", settings.fingerprint_webgl ? "1" : "0");
    command_line->AppendSwitchWithValue("kiwi-fp-canvas", settings.fingerprint_canvas ? "1" : "0");
    command_line->AppendSwitchWithValue("kiwi-fp-referrer", settings.fingerprint_referrer);
    command_line->AppendSwitchWithValue("kiwi-gpc", settings.gpc ? "1" : "0");
    if (!rt->site_rules_path.empty()) {
        // The render process has no runtime(): it loads the same store
        // read-only from this path to resolve per-site fingerprint rules.
        command_line->AppendSwitchWithValue("kiwi-siterules", rt->site_rules_path);
    }
}

void KiwiCefApp::OnContextCreated(CefRefPtr<CefBrowser> browser,
                                  CefRefPtr<CefFrame> frame,
                                  CefRefPtr<CefV8Context> context) {
    (void)browser;
    inject_scripts(context, frame->GetURL().ToString());
}

void KiwiCefApp::inject_scripts(CefRefPtr<CefV8Context> context,
                                const std::string& frame_url) {
    CefRefPtr<CefCommandLine> command_line = CefCommandLine::GetGlobalCommandLine();
    bool webgl = switch_value(command_line, "kiwi-fp-webgl") == "1";
    bool canvas = switch_value(command_line, "kiwi-fp-canvas") == "1";
    const bool gpc = switch_value(command_line, "kiwi-gpc") == "1";
    std::string referrer = switch_value(command_line, "kiwi-fp-referrer");

    // Per-site fingerprint overrides resolved against the same sites.toml
    // the GTK build and the browser process use.
    const std::string rules_path = switch_value(command_line, "kiwi-siterules");
    if (!rules_path.empty()) {
        static mustela::SiteRules rules(rules_path);
        static bool loaded = false;
        if (!loaded) {
            rules.load();
            loaded = true;
        }
        const mustela::SitePolicy* site =
            rules.find(mustela::host_from_uri(frame_url));
        if (site && site->fingerprint == mustela::SiteRule::Allow) {
            webgl = false;
            canvas = false;
            referrer.clear();
        } else if (site && site->fingerprint == mustela::SiteRule::Deny) {
            webgl = true;
            canvas = true;
            referrer = "never";
        }
    }

    if (!webgl && !canvas && !gpc && referrer.empty()) return;

    context->Enter();
    auto eval = [&context](const char* code) {
        CefRefPtr<CefV8Value> retval;
        CefRefPtr<CefV8Exception> exception;
        context->Eval(code, CefString(), 0, retval, exception);
    };
    if (webgl) eval(mustela::fp_scripts::WEBGL_BLOCK);
    if (canvas) eval(mustela::fp_scripts::CANVAS_BLANK);
    if (gpc) eval(mustela::gpc_user_script());
    if (!referrer.empty()) {
        const std::string script = mustela::fingerprint_referrer_script(
            mustela::referrer_trim_from(referrer));
        if (!script.empty()) eval(script.c_str());
    }
    context->Exit();
}

}
