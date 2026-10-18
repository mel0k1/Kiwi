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
}

void KiwiCefApp::OnContextCreated(CefRefPtr<CefBrowser> browser,
                                  CefRefPtr<CefFrame> frame,
                                  CefRefPtr<CefV8Context> context) {
    (void)browser;
    (void)frame;
    inject_scripts(context);
}

void KiwiCefApp::inject_scripts(CefRefPtr<CefV8Context> context) {
    CefRefPtr<CefCommandLine> command_line = CefCommandLine::GetGlobalCommandLine();
    const bool webgl = switch_value(command_line, "kiwi-fp-webgl") == "1";
    const bool canvas = switch_value(command_line, "kiwi-fp-canvas") == "1";
    const bool gpc = switch_value(command_line, "kiwi-gpc") == "1";
    const std::string referrer = switch_value(command_line, "kiwi-fp-referrer");

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
