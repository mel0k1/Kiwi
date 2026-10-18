#pragma once

#include <string>

#include "include/cef_browser.h"

namespace kiwi {

enum Hotkey {
    HK_NONE,
    HK_NEW_TAB,
    HK_CLOSE_TAB,
    HK_FOCUS_OMNIBOX,
    HK_RELOAD,
    HK_BOOKMARK,
    HK_PRIVATE_TAB,
    HK_BACK,
    HK_FORWARD,
    HK_FIND,
    HK_FIND_NEXT,
    HK_FIND_PREV,
    HK_DEVTOOLS
};

// Shell-facing callbacks the CEF client forwards. Every method runs on the
// browser-process UI thread, so implementations may touch windows directly.
class TabHost {
public:
    virtual ~TabHost() = default;

    virtual void on_created(CefRefPtr<CefBrowser> browser) = 0;
    virtual void on_closed(int browser_id) = 0;
    virtual void on_title(int browser_id, const std::string& title) = 0;
    virtual void on_address(int browser_id, const std::string& url) = 0;
    virtual void on_loading(int browser_id, bool loading) = 0;
    virtual void on_status(int browser_id, const std::string& text) = 0;
    virtual void on_popup(int browser_id, const std::string& url, bool private_tab) = 0;
    virtual void on_hotkey(Hotkey key) = 0;
    virtual void on_find_result(int browser_id, int count, int active_match,
                                bool final_update) = 0;
};

// Host part of a URL: strips scheme, userinfo, port and query, lowercases.
std::string url_host(const std::string& url);

}
