#pragma once

#include "include/cef_app.h"

namespace kiwi {

// Single CefApp for both browser and render processes. The browser process
// side trims Chromium services Kiwi does not want, wires DoH and passes the
// fingerprint/GPC flags down to renderers; the render process side injects
// mustela-core privacy scripts before any page script runs.
class KiwiCefApp : public CefApp,
                   public CefBrowserProcessHandler,
                   public CefRenderProcessHandler {
public:
    KiwiCefApp() = default;

    void OnBeforeCommandLineProcessing(const CefString& process_type,
                                       CefRefPtr<CefCommandLine> command_line) override;

    CefRefPtr<CefBrowserProcessHandler> GetBrowserProcessHandler() override { return this; }
    CefRefPtr<CefRenderProcessHandler> GetRenderProcessHandler() override { return this; }

    void OnContextCreated(CefRefPtr<CefBrowser> browser,
                          CefRefPtr<CefFrame> frame,
                          CefRefPtr<CefV8Context> context) override;

private:
    void inject_scripts(CefRefPtr<CefV8Context> context, const std::string& frame_url);

    IMPLEMENT_REFCOUNTING(KiwiCefApp);
    DISALLOW_COPY_AND_ASSIGN(KiwiCefApp);
};

}
