#pragma once

#include "cef_bridge.hpp"

#include "include/cef_client.h"
#include "include/cef_download_handler.h"
#include "include/cef_find_handler.h"
#include "include/cef_permission_handler.h"
#include "include/cef_request_handler.h"
#include "include/cef_resource_request_handler.h"

namespace kiwi {

// Per-tab client: lifespan/display wiring to the shell, plus the whole
// request-level privacy surface — tracker blocklist, ping and font blocking,
// third-party cookie filtering, media-access denial, downloads into the
// user downloads directory and the keyboard shortcut bridge.
// Minimal client for the DevTools popup browser. It intentionally forwards
// nothing to the shell: the DevTools browser must never land in the tab
// strip or drive the omnibox.
class DevToolsClient : public CefClient {
public:
    DevToolsClient() = default;

private:
    IMPLEMENT_REFCOUNTING(DevToolsClient);
    DISALLOW_COPY_AND_ASSIGN(DevToolsClient);
};

class KiwiClient : public CefClient,
                   public CefLifeSpanHandler,
                   public CefDisplayHandler,
                   public CefLoadHandler,
                   public CefRequestHandler,
                   public CefResourceRequestHandler,
                   public CefCookieAccessFilter,
                   public CefDownloadHandler,
                   public CefPermissionHandler,
                   public CefKeyboardHandler,
                   public CefFindHandler {
public:
    KiwiClient(TabHost* host, bool private_tab);

    CefRefPtr<CefLifeSpanHandler> GetLifeSpanHandler() override { return this; }
    CefRefPtr<CefDisplayHandler> GetDisplayHandler() override { return this; }
    CefRefPtr<CefLoadHandler> GetLoadHandler() override { return this; }
    CefRefPtr<CefRequestHandler> GetRequestHandler() override { return this; }
    CefRefPtr<CefDownloadHandler> GetDownloadHandler() override { return this; }
    CefRefPtr<CefPermissionHandler> GetPermissionHandler() override { return this; }
    CefRefPtr<CefKeyboardHandler> GetKeyboardHandler() override { return this; }
    CefRefPtr<CefFindHandler> GetFindHandler() override { return this; }

    void OnAfterCreated(CefRefPtr<CefBrowser> browser) override;
    bool OnBeforePopup(CefRefPtr<CefBrowser> browser,
                       CefRefPtr<CefFrame> frame,
                       int popup_id,
                       const CefString& target_url,
                       const CefString& target_frame_name,
                       cef_window_open_disposition_t target_disposition,
                       bool user_gesture,
                       const CefPopupFeatures& popupFeatures,
                       CefWindowInfo& windowInfo,
                       CefRefPtr<CefClient>& client,
                       CefBrowserSettings& settings,
                       CefRefPtr<CefDictionaryValue>& extra_info,
                       bool* no_javascript_access) override;
    void OnBeforeClose(CefRefPtr<CefBrowser> browser) override;

    void OnTitleChange(CefRefPtr<CefBrowser> browser,
                       const CefString& title) override;
    void OnAddressChange(CefRefPtr<CefBrowser> browser,
                         CefRefPtr<CefFrame> frame,
                         const CefString& url) override;
    void OnLoadingStateChange(CefRefPtr<CefBrowser> browser,
                              bool is_loading,
                              bool can_go_back,
                              bool can_go_forward) override;
    void OnStatusMessage(CefRefPtr<CefBrowser> browser, const CefString& value) override;

    CefRefPtr<CefResourceRequestHandler> GetResourceRequestHandler(
        CefRefPtr<CefBrowser> browser,
        CefRefPtr<CefFrame> frame,
        CefRefPtr<CefRequest> request,
        bool is_navigation,
        bool is_download,
        const CefString& request_initiator,
        bool& disable_default_handling) override;

    CefRefPtr<CefCookieAccessFilter> GetCookieAccessFilter(
        CefRefPtr<CefBrowser> browser,
        CefRefPtr<CefFrame> frame,
        CefRefPtr<CefRequest> request) override;

    cef_return_value_t OnBeforeResourceLoad(CefRefPtr<CefBrowser> browser,
                                     CefRefPtr<CefFrame> frame,
                                     CefRefPtr<CefRequest> request,
                                     CefRefPtr<CefCallback> callback) override;

    bool CanSendCookie(CefRefPtr<CefBrowser> browser,
                       CefRefPtr<CefFrame> frame,
                       CefRefPtr<CefRequest> request,
                       const CefCookie& cookie) override;
    bool CanSaveCookie(CefRefPtr<CefBrowser> browser,
                       CefRefPtr<CefFrame> frame,
                       CefRefPtr<CefRequest> request,
                       CefRefPtr<CefResponse> response,
                       const CefCookie& cookie) override;

    bool OnBeforeDownload(CefRefPtr<CefBrowser> browser,
                          CefRefPtr<CefDownloadItem> download_item,
                          const CefString& suggested_name,
                          CefRefPtr<CefBeforeDownloadCallback> callback) override;

    bool OnRequestMediaAccessPermission(CefRefPtr<CefBrowser> browser,
                                        CefRefPtr<CefFrame> frame,
                                        const CefString& requesting_origin,
                                        uint32_t requested_permissions,
                                        CefRefPtr<CefMediaAccessCallback> callback) override;

    bool OnPreKeyEvent(CefRefPtr<CefBrowser> browser,
                       const CefKeyEvent& event,
                       CefEventHandle os_event,
                       bool* is_keyboard_shortcut) override;

    void OnFindResult(CefRefPtr<CefBrowser> browser,
                      int identifier,
                      int count,
                      const CefRect& selectionRect,
                      int activeMatchOrdinal,
                      bool finalUpdate) override;

private:
    bool cookie_allowed(CefRefPtr<CefRequest> request, const CefCookie& cookie) const;

    TabHost* host_;
    bool private_tab_;
    int browser_id_ = 0;

    IMPLEMENT_REFCOUNTING(KiwiClient);
    DISALLOW_COPY_AND_ASSIGN(KiwiClient);
};

}
