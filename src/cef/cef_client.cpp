#include "cef_client.hpp"

#include "cef_runtime.hpp"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include "include/cef_command_ids.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>

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

// True when the URL path targets a JavaScript resource. Per-site JS-denied
// sites get their external scripts cancelled here; inline scripts still run
// (Chromium has no per-origin script toggle — documented as a partial).
bool is_script_url(const std::string& url) {
    size_t path_start = url.find("://");
    std::string rest = path_start == std::string::npos ? url : url.substr(path_start + 3);
    size_t slash = rest.find('/');
    if (slash == std::string::npos) return false;
    size_t query = rest.find_first_of("?#", slash);
    std::string path = query == std::string::npos
        ? rest.substr(slash)
        : rest.substr(slash, query - slash);
    if (path.size() < 3) return false;
    std::string tail = path.substr(path.size() - 3);
    for (char& c : tail) {
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    }
    return tail == ".js";
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

void KiwiClient::OnLoadEnd(CefRefPtr<CefBrowser> browser,
                           CefRefPtr<CefFrame> frame,
                           int httpStatusCode) {
    (void)httpStatusCode;
    Runtime* rt = runtime();
    if (!rt || !rt->site_rules || !frame->IsMain()) return;
    const mustela::SiteDecision site = mustela::resolve_site(
        rt->policy, *rt->site_rules, frame->GetURL().ToString());
    // Chromium zoom level = log(factor) / log(1.2); 0.0 means 100%.
    const double level = site.zoom > 0.0
        ? std::log(site.zoom) / std::log(1.2)
        : 0.0;
    browser->GetHost()->SetZoomLevel(level);
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
    Runtime* rt = runtime();
    if (!rt) return nullptr;
    if (rt->policy.cookies != mustela::CookiePolicy::Always) return this;
    if (rt->site_rules) {
        // A per-site cookie deny needs the filter even under a global Always
        // policy.
        const mustela::SitePolicy* site =
            rt->site_rules->find(url_host(request->GetURL().ToString()));
        if (site && site->cookies == mustela::SiteRule::Deny) return this;
    }
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

    const std::string url = request->GetURL().ToString();
    if (!rt->site_rules) return RV_CONTINUE;
    const mustela::SiteDecision site =
        mustela::resolve_site(rt->policy, *rt->site_rules, url);

    if (site.fingerprint.block_remote_fonts
        && request->GetResourceType() == RT_FONT_RESOURCE) {
        return RV_CANCEL;
    }

    if (!site.javascript
        && (request->GetResourceType() == RT_SCRIPT || is_script_url(url))) {
        return RV_CANCEL;
    }

    if (rt->policy.blocklist) {
        const std::string host = url_host(url);
        if (!host.empty() && rt->blocklist.hit(mustela::normalize_host(host))) {
            return RV_CANCEL;
        }
    }

    if (rt->policy.gpc) {
        request->SetHeaderByName("Sec-GPC", "1", true);
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
    if (is_third_party(host, domain)) return false;
    if (rt->site_rules) {
        // SiteRule::Deny for cookies blocks everything for that site,
        // regardless of the global third-party policy.
        if (rt->site_rules->find(host) != nullptr
            && rt->site_rules->find(host)->cookies == mustela::SiteRule::Deny) {
            return false;
        }
    }
    return true;
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
            case VK_OEM_COMMA: key = HK_SETTINGS; break;
            default: break;
        }
    } else if (ctrl && shift && !alt) {
        if (event.windows_key_code == 'P') key = HK_PRIVATE_TAB;
        if (event.windows_key_code == 'I') key = HK_DEVTOOLS;
        if (event.windows_key_code == 'O') key = HK_BOOKMARKS_WINDOW;
        if (event.windows_key_code == 'H') key = HK_HISTORY_WINDOW;
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

void KiwiClient::OnFaviconURLChange(CefRefPtr<CefBrowser> browser,
                                    const std::vector<CefString>& icon_urls) {
    if (!host_) return;
    std::vector<std::string> urls;
    urls.reserve(icon_urls.size());
    for (const CefString& url : icon_urls) urls.push_back(url.ToString());
    host_->on_favicon_urls(browser->GetIdentifier(), urls);
}

namespace {

constexpr int CMD_BACK = MENU_ID_USER_FIRST + 1;
constexpr int CMD_FORWARD = MENU_ID_USER_FIRST + 2;
constexpr int CMD_RELOAD = MENU_ID_USER_FIRST + 3;
constexpr int CMD_STOP = MENU_ID_USER_FIRST + 4;
constexpr int CMD_OPEN_LINK_NEW_TAB = MENU_ID_USER_FIRST + 10;
constexpr int CMD_COPY_LINK = MENU_ID_USER_FIRST + 11;
constexpr int CMD_OPEN_IMAGE_NEW_TAB = MENU_ID_USER_FIRST + 12;
constexpr int CMD_COPY_IMAGE_URL = MENU_ID_USER_FIRST + 13;
constexpr int CMD_COPY_SELECTION = MENU_ID_USER_FIRST + 20;
constexpr int CMD_SEARCH_SELECTION = MENU_ID_USER_FIRST + 21;
constexpr int CMD_UNDO = MENU_ID_USER_FIRST + 22;
constexpr int CMD_CUT = MENU_ID_USER_FIRST + 23;
constexpr int CMD_PASTE = MENU_ID_USER_FIRST + 24;
constexpr int CMD_SELECT_ALL = MENU_ID_USER_FIRST + 25;
constexpr int CMD_VIEW_SOURCE = MENU_ID_USER_FIRST + 30;
constexpr int CMD_INSPECT = MENU_ID_USER_FIRST + 31;

std::wstring utf8_to_wide(const std::string& text) {
    if (text.empty()) return std::wstring();
    int size = MultiByteToWideChar(CP_UTF8, 0, text.c_str(),
        static_cast<int>(text.size()), nullptr, 0);
    std::wstring wide(static_cast<size_t>(size), L'\0');
    if (size > 0) {
        MultiByteToWideChar(CP_UTF8, 0, text.c_str(),
            static_cast<int>(text.size()), wide.data(), size);
    }
    return wide;
}

void copy_text_to_clipboard(HWND owner, const std::string& utf8) {
    std::wstring text = utf8_to_wide(utf8);
    if (text.empty()) return;
    if (!OpenClipboard(owner)) return;
    EmptyClipboard();
    HGLOBAL mem = GlobalAlloc(GMEM_MOVEABLE, (text.size() + 1) * sizeof(wchar_t));
    if (mem) {
        void* dest = GlobalLock(mem);
        if (dest) {
            std::memcpy(dest, text.c_str(), (text.size() + 1) * sizeof(wchar_t));
            GlobalUnlock(mem);
            if (!SetClipboardData(CF_UNICODETEXT, mem)) GlobalFree(mem);
        } else {
            GlobalFree(mem);
        }
    }
    CloseClipboard();
}

std::string percent_encode_utf8(const std::string& raw) {
    static const char hex[] = "0123456789ABCDEF";
    std::string out;
    for (unsigned char c : raw) {
        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9')
            || c == '-' || c == '_' || c == '.' || c == '~') {
            out += static_cast<char>(c);
        } else {
            out += '%';
            out += hex[c >> 4];
            out += hex[c & 0x0F];
        }
    }
    return out;
}

std::string engine_template() {
    Runtime* rt = runtime();
    if (rt) {
        for (const mustela::SearchEngine& engine : rt->settings.search_engines) {
            if (!rt->settings.search_default.empty()
                && engine.keyword == rt->settings.search_default
                && engine.url_template.find("%s") != std::string::npos) {
                return engine.url_template;
            }
        }
        if (rt->settings.search_template.find("%s") != std::string::npos) {
            return rt->settings.search_template;
        }
        for (const mustela::SearchEngine& engine : rt->settings.search_engines) {
            if (engine.url_template.find("%s") != std::string::npos) {
                return engine.url_template;
            }
        }
    }
    return "https://duckduckgo.com/?q=%s";
}

std::string replace_first(std::string text, const std::string& slot,
                          const std::string& value) {
    size_t at = text.find(slot);
    if (at == std::string::npos) return text;
    return text.substr(0, at) + value + text.substr(at + slot.size());
}

bool is_web_url(const std::string& url) {
    return url.rfind("http://", 0) == 0 || url.rfind("https://", 0) == 0;
}

}  // namespace

void KiwiClient::OnBeforeContextMenu(CefRefPtr<CefBrowser> browser,
                                     CefRefPtr<CefFrame> frame,
                                     CefRefPtr<CefContextMenuParams> params,
                                     CefRefPtr<CefMenuModel> model) {
    if (!host_) return;
    model->Clear();

    const std::string page_url = frame->GetURL().ToString();
    const bool web = is_web_url(page_url);
    const int types = params->GetTypeFlags();
    const bool link = (types & CM_TYPEFLAG_LINK) != 0;
    const bool image = params->GetMediaType() == CM_MEDIATYPE_IMAGE;
    const bool selection = (types & CM_TYPEFLAG_SELECTION) != 0
        && !params->GetSelectionText().empty();
    const bool editable = (types & CM_TYPEFLAG_EDITABLE) != 0;

    if (web) {
        model->AddItem(CMD_BACK, L"Back");
        model->SetEnabled(CMD_BACK, browser->CanGoBack());
        model->AddItem(CMD_FORWARD, L"Forward");
        model->SetEnabled(CMD_FORWARD, browser->CanGoForward());
        model->AddItem(CMD_RELOAD, L"Reload");
        model->AddItem(CMD_STOP, L"Stop");
        model->SetEnabled(CMD_STOP, browser->IsLoading());
        model->AddSeparator();
    }

    if (link) {
        model->AddItem(CMD_OPEN_LINK_NEW_TAB, L"Open link in new tab");
        model->AddItem(CMD_COPY_LINK, L"Copy link address");
        model->AddSeparator();
    }

    if (image) {
        model->AddItem(CMD_OPEN_IMAGE_NEW_TAB, L"Open image in new tab");
        model->AddItem(CMD_COPY_IMAGE_URL, L"Copy image address");
        model->AddSeparator();
    }

    if (editable) {
        const int edit = params->GetEditStateFlags();
        model->AddItem(CMD_UNDO, L"Undo");
        model->SetEnabled(CMD_UNDO, (edit & CM_EDITFLAG_CAN_UNDO) != 0);
        model->AddItem(CMD_CUT, L"Cut");
        model->SetEnabled(CMD_CUT, (edit & CM_EDITFLAG_CAN_CUT) != 0);
        model->AddItem(CMD_COPY_SELECTION, L"Copy");
        model->SetEnabled(CMD_COPY_SELECTION,
            (edit & CM_EDITFLAG_CAN_COPY) != 0);
        model->AddItem(CMD_PASTE, L"Paste");
        model->SetEnabled(CMD_PASTE, (edit & CM_EDITFLAG_CAN_PASTE) != 0);
        model->AddItem(CMD_SELECT_ALL, L"Select all");
        model->SetEnabled(CMD_SELECT_ALL,
            (edit & CM_EDITFLAG_CAN_SELECT_ALL) != 0);
        model->AddSeparator();
    } else if (selection) {
        model->AddItem(CMD_COPY_SELECTION, L"Copy");
        model->AddItem(CMD_SEARCH_SELECTION, L"Search web for selection");
        model->AddSeparator();
    }

    if (web) {
        model->AddItem(CMD_VIEW_SOURCE, L"View page source");
        if (browser->GetHost()->HasDevTools()) {
            model->AddItem(CMD_INSPECT, L"Inspect");
        }
    }
}

bool KiwiClient::OnContextMenuCommand(CefRefPtr<CefBrowser> browser,
                                      CefRefPtr<CefFrame> frame,
                                      CefRefPtr<CefContextMenuParams> params,
                                      int command_id,
                                      cef_event_flags_t event_flags) {
    (void)event_flags;
    if (!host_) return false;
    switch (command_id) {
        case CMD_BACK:
            browser->GoBack();
            return true;
        case CMD_FORWARD:
            browser->GoForward();
            return true;
        case CMD_RELOAD:
            browser->Reload();
            return true;
        case CMD_STOP:
            browser->StopLoad();
            return true;
        case CMD_OPEN_LINK_NEW_TAB:
            host_->on_open_url(browser->GetIdentifier(),
                params->GetLinkUrl().ToString(), true);
            return true;
        case CMD_COPY_LINK: {
            std::string link = params->GetUnfilteredLinkUrl().ToString();
            if (link.empty()) link = params->GetLinkUrl().ToString();
            copy_text_to_clipboard(
                browser->GetHost()->GetWindowHandle(), link);
            return true;
        }
        case CMD_OPEN_IMAGE_NEW_TAB:
            host_->on_open_url(browser->GetIdentifier(),
                params->GetSourceUrl().ToString(), true);
            return true;
        case CMD_COPY_IMAGE_URL:
            copy_text_to_clipboard(browser->GetHost()->GetWindowHandle(),
                params->GetSourceUrl().ToString());
            return true;
        case CMD_COPY_SELECTION:
            frame->Copy();
            return true;
        case CMD_SEARCH_SELECTION: {
            std::string query = params->GetSelectionText().ToString();
            if (query.empty()) return true;
            std::string url = replace_first(engine_template(), "%s",
                percent_encode_utf8(query));
            host_->on_open_url(browser->GetIdentifier(), url, true);
            return true;
        }
        case CMD_UNDO:
            frame->Undo();
            return true;
        case CMD_CUT:
            frame->Cut();
            return true;
        case CMD_PASTE:
            frame->Paste();
            return true;
        case CMD_SELECT_ALL:
            frame->SelectAll();
            return true;
        case CMD_VIEW_SOURCE:
            frame->ViewSource();
            return true;
        case CMD_INSPECT:
            host_->on_hotkey(HK_DEVTOOLS);
            return true;
        default:
            return false;
    }
}

void KiwiClient::OnContextMenuDismissed(CefRefPtr<CefBrowser> browser,
                                        CefRefPtr<CefFrame> frame) {
    (void)browser;
    (void)frame;
}

}
