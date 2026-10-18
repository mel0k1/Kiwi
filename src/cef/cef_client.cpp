#include "cef_client.hpp"

#include "cef_runtime.hpp"

#include <algorithm>
#include <cctype>

namespace kiwi {

std::string url_host(const std::string& url) {
    size_t scheme = url.find("://");
    std::string rest = scheme == std::string::npos ? url : url.substr(scheme + 3);
    size_t slash = rest.find_first_of("/?#");
    if (slash != std::string::npos) rest.resize(slash);
    size_t at = rest.find('@');
    if (at != std::string::npos) rest = rest.substr(at + 1);
    size_t colon = rest.find(':');
    if (colon != std::string::npos) rest.resize(colon);
    for (char& c : rest) {
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    }
    return rest;
}

namespace {

constexpr unsigned int kVkLeft = 0x25;
constexpr unsigned int kVkRight = 0x27;
constexpr unsigned int kVkF5 = 0x74;
constexpr unsigned int kVkF3 = 0x72;

bool is_third_party(const std::string& request_host, const std::string& cookie_domain) {
    if (request_host.empty() || cookie_domain.empty()) return false;
    std::string domain = cookie_domain;
    if (!domain.empty() && domain.front() == '.') domain.erase(domain.begin());
    for (char& c : domain) {
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    }
    return !mustela::host_matches_domain(request_host, domain)
        && !mustela::host_matches_domain(domain, request_host);
}

std::string sanitize_file_name(const std::string& name) {
    std::string out;
    out.reserve(name.size());
    for (char c : name) {
        if (c == '/' || c == '\\' || c == ':' || c == '*' || c == '?' || c == '"'
            || c == '<' || c == '>' || c == '|') {
            out += '_';
        } else if (static_cast<unsigned char>(c) >= 0x20) {
            out += c;
        }
    }
    return out.empty() ? std::string("download.bin") : out;
}

std::string join_path(const std::string& dir, const std::string& name) {
    if (dir.empty()) return name;
    char last = dir.back();
    if (last == '/' || last == '\\') return dir + name;
    return dir + "/" + name;
}

}  // namespace

KiwiClient::KiwiClient(TabHost* host, bool private_tab)
    : host_(host), private_tab_(private_tab) {}

void KiwiClient::OnAfterCreated(CefRefPtr<CefBrowser> browser) {
    browser_id_ = browser->GetIdentifier();
    if (host_) host_->on_created(browser);
}

bool KiwiClient::OnBeforePopup(CefRefPtr<CefBrowser> browser,
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
                               bool* no_javascript_access) {
    (void)frame;
    (void)popup_id;
    (void)target_frame_name;
    (void)target_disposition;
    (void)user_gesture;
    (void)popupFeatures;
    (void)windowInfo;
    (void)client;
    (void)settings;
    (void)extra_info;
    (void)no_javascript_access;
    if (!host_) return true;
    host_->on_popup(browser->GetIdentifier(), target_url.ToString(), private_tab_);
    return true;
}

void KiwiClient::OnBeforeClose(CefRefPtr<CefBrowser> browser) {
    if (host_) host_->on_closed(browser->GetIdentifier());
}

void KiwiClient::OnTitleChange(CefRefPtr<CefBrowser> browser,
                               const CefString& title) {
    if (!host_) return;
    host_->on_title(browser->GetIdentifier(), title.ToString());
}

void KiwiClient::OnAddressChange(CefRefPtr<CefBrowser> browser,
                                 CefRefPtr<CefFrame> frame,
                                 const CefString& url) {
    if (!frame->IsMain() || !host_) return;
    host_->on_address(browser->GetIdentifier(), url.ToString());
}

void KiwiClient::OnLoadingStateChange(CefRefPtr<CefBrowser> browser,
                                      bool is_loading,
                                      bool can_go_back,
                                      bool can_go_forward) {
    (void)can_go_back;
    (void)can_go_forward;
    if (host_) host_->on_loading(browser->GetIdentifier(), is_loading);
}

void KiwiClient::OnStatusMessage(CefRefPtr<CefBrowser> browser, const CefString& value) {
    if (host_) host_->on_status(browser->GetIdentifier(), value.ToString());
}

CefRefPtr<CefResourceRequestHandler> KiwiClient::GetResourceRequestHandler(
    CefRefPtr<CefBrowser> browser,
    CefRefPtr<CefFrame> frame,
    CefRefPtr<CefRequest> request,
    bool is_navigation,
    bool is_download,
    const CefString& request_initiator,
    bool& disable_default_handling) {
    (void)browser;
    (void)frame;
    (void)request;
    (void)is_navigation;
    (void)is_download;
    (void)request_initiator;
    (void)disable_default_handling;
    return this;
}

CefRefPtr<CefCookieAccessFilter> KiwiClient::GetCookieAccessFilter(
    CefRefPtr<CefBrowser> browser,
    CefRefPtr<CefFrame> frame,
    CefRefPtr<CefRequest> request) {
    (void)browser;
    (void)frame;
    (void)request;
    Runtime* rt = runtime();
    if (rt && rt->policy.cookies != mustela::CookiePolicy::Always) return this;
    return nullptr;
}

cef_return_value_t KiwiClient::OnBeforeResourceLoad(CefRefPtr<CefBrowser> browser,
                                                    CefRefPtr<CefFrame> frame,
                                                    CefRefPtr<CefRequest> request,
                                                    CefRefPtr<CefCallback> callback) {
    (void)browser;
    (void)frame;
    (void)callback;
    Runtime* rt = runtime();
    if (!rt) return RV_CONTINUE;

    if (!rt->policy.hardening.hyperlink_auditing
        && request->GetMethod().ToString() == "PING") {
        return RV_CANCEL;
    }

    if (rt->policy.fingerprint.block_remote_fonts
        && request->GetResourceType() == RT_FONT_RESOURCE) {
        return RV_CANCEL;
    }

    if (rt->policy.blocklist) {
        const std::string host = url_host(request->GetURL().ToString());
        if (!host.empty() && rt->blocklist.hit(mustela::normalize_host(host))) {
            return RV_CANCEL;
        }
    }

    return RV_CONTINUE;
}

bool KiwiClient::cookie_allowed(CefRefPtr<CefRequest> request,
                                const CefCookie& cookie) const {
    Runtime* rt = runtime();
    if (!rt) return true;
    if (rt->policy.cookies == mustela::CookiePolicy::Never) return false;
    const std::string host = url_host(request->GetURL().ToString());
    const std::string domain = CefString(&cookie.domain).ToString();
    return !is_third_party(host, domain);
}

bool KiwiClient::CanSendCookie(CefRefPtr<CefBrowser> browser,
                               CefRefPtr<CefFrame> frame,
                               CefRefPtr<CefRequest> request,
                               const CefCookie& cookie) {
    (void)browser;
    (void)frame;
    return cookie_allowed(request, cookie);
}

bool KiwiClient::CanSaveCookie(CefRefPtr<CefBrowser> browser,
                               CefRefPtr<CefFrame> frame,
                               CefRefPtr<CefRequest> request,
                               CefRefPtr<CefResponse> response,
                               const CefCookie& cookie) {
    (void)browser;
    (void)frame;
    (void)response;
    return cookie_allowed(request, cookie);
}

bool KiwiClient::OnBeforeDownload(CefRefPtr<CefBrowser> browser,
                                  CefRefPtr<CefDownloadItem> download_item,
                                  const CefString& suggested_name,
                                  CefRefPtr<CefBeforeDownloadCallback> callback) {
    (void)browser;
    (void)download_item;
    Runtime* rt = runtime();
    const std::string dir = rt ? rt->downloads_dir : std::string(".");
    const std::string name = sanitize_file_name(suggested_name.ToString());
    callback->Continue(join_path(dir, name), false);
    return true;
}

bool KiwiClient::OnRequestMediaAccessPermission(CefRefPtr<CefBrowser> browser,
                                                CefRefPtr<CefFrame> frame,
                                                const CefString& requesting_origin,
                                                uint32_t requested_permissions,
                                                CefRefPtr<CefMediaAccessCallback> callback) {
    (void)browser;
    (void)frame;
    (void)requesting_origin;
    (void)requested_permissions;
    callback->Cancel();
    return true;
}

bool KiwiClient::OnPreKeyEvent(CefRefPtr<CefBrowser> browser,
                               const CefKeyEvent& event,
                               CefEventHandle os_event,
                               bool* is_keyboard_shortcut) {
    (void)browser;
    (void)os_event;
    (void)is_keyboard_shortcut;
    if (!host_ || event.type != KEYEVENT_RAWKEYDOWN) return false;

    const bool ctrl = (event.modifiers & EVENTFLAG_CONTROL_DOWN) != 0;
    const bool shift = (event.modifiers & EVENTFLAG_SHIFT_DOWN) != 0;
    const bool alt = (event.modifiers & EVENTFLAG_ALT_DOWN) != 0;

    Hotkey key = HK_NONE;
    if (ctrl && !shift && !alt) {
        switch (event.windows_key_code) {
            case 'T': key = HK_NEW_TAB; break;
            case 'W': key = HK_CLOSE_TAB; break;
            case 'L': key = HK_FOCUS_OMNIBOX; break;
            case 'R': key = HK_RELOAD; break;
            case 'D': key = HK_BOOKMARK; break;
            case 'F': key = HK_FIND; break;
            default: break;
        }
    } else if (ctrl && shift && !alt) {
        if (event.windows_key_code == 'P') key = HK_PRIVATE_TAB;
        if (event.windows_key_code == 'I') key = HK_DEVTOOLS;
    } else if (shift && !ctrl && !alt) {
        if (event.windows_key_code == kVkF3) key = HK_FIND_PREV;
    } else if (alt && !ctrl && !shift) {
        if (event.windows_key_code == kVkLeft) key = HK_BACK;
        if (event.windows_key_code == kVkRight) key = HK_FORWARD;
    } else if (!ctrl && !shift && !alt) {
        if (event.windows_key_code == kVkF5) key = HK_RELOAD;
        if (event.windows_key_code == kVkF3) key = HK_FIND_NEXT;
    }

    if (key == HK_NONE) return false;
    host_->on_hotkey(key);
    return true;
}

void KiwiClient::OnFindResult(CefRefPtr<CefBrowser> browser,
                              int identifier,
                              int count,
                              const CefRect& selectionRect,
                              int activeMatchOrdinal,
                              bool finalUpdate) {
    (void)identifier;
    (void)selectionRect;
    if (!host_) return;
    host_->on_find_result(browser->GetIdentifier(), count, activeMatchOrdinal, finalUpdate);
}

}
