#include "updater_win.hpp"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <shellapi.h>

#include "include/cef_app.h"
#include "include/cef_urlrequest.h"
#include "include/cef_request_context.h"

#include <algorithm>
#include <string>

#ifndef KIWI_VERSION
#define KIWI_VERSION "0.0.0"
#endif

namespace kiwi {

namespace {

struct Version {
    int major = 0;
    int minor = 0;
    int patch = 0;
};

Version parse_version(const std::string& text) {
    // Accepts "v0.14.0", "0.14.0" and "0.14.0-suffix" (suffix ignored).
    Version v;
    size_t start = text.find_first_of("0123456789");
    if (start == std::string::npos) return v;
    int fields[3] = {0, 0, 0};
    int idx = 0;
    std::string digits;
    for (size_t i = start; i < text.size(); ++i) {
        char c = text[i];
        if (c >= '0' && c <= '9') {
            digits += c;
        } else if (c == '.' && !digits.empty() && idx < 3) {
            fields[idx++] = std::atoi(digits.c_str());
            digits.clear();
        } else {
            break;
        }
    }
    if (idx < 3 && !digits.empty()) fields[idx] = std::atoi(digits.c_str());
    v.major = fields[0];
    v.minor = fields[1];
    v.patch = fields[2];
    return v;
}

bool newer(const Version& latest, const Version& current) {
    if (latest.major != current.major) return latest.major > current.major;
    if (latest.minor != current.minor) return latest.minor > current.minor;
    return latest.patch > current.patch;
}

std::string json_string_field(const std::string& json, const std::string& field) {
    std::string needle = "\"" + field + "\"";
    size_t pos = json.find(needle);
    if (pos == std::string::npos) return std::string();
    pos = json.find(':', pos + needle.size());
    if (pos == std::string::npos) return std::string();
    pos = json.find('"', pos);
    if (pos == std::string::npos) return std::string();
    size_t end = json.find('"', pos + 1);
    if (end == std::string::npos) return std::string();
    return json.substr(pos + 1, end - pos - 1);
}

void open_releases_page() {
    ShellExecuteW(nullptr, L"open",
        L"https://github.com/mel0k1/Kiwi/releases/latest", nullptr, nullptr,
        SW_SHOWNORMAL);
}

class UpdateRequestClient : public CefURLRequestClient {
public:
    explicit UpdateRequestClient(HWND owner) : owner_(owner) {}

    void OnRequestComplete(CefRefPtr<CefURLRequest> request) override {
        std::string tag;
        if (request->GetRequestStatus() == UR_SUCCESS) {
            tag = json_string_field(body_, "tag_name");
        }
        if (tag.empty()) {
            MessageBoxW(owner_, L"Could not check for updates (network error).",
                L"Kiwi update check", MB_OK | MB_ICONWARNING);
            return;
        }
        Version latest = parse_version(tag);
        Version current = parse_version(KIWI_VERSION);
        std::wstring wide_tag(tag.begin(), tag.end());
        if (newer(latest, current)) {
            std::wstring message = L"A new Kiwi version is available: "
                + wide_tag + L" (you have " + L"" KIWI_VERSION L").\n\n"
                L"Open the releases page?";
            if (MessageBoxW(owner_, message.c_str(), L"Kiwi update check",
                    MB_YESNO | MB_ICONINFORMATION) == IDYES) {
                open_releases_page();
            }
        } else {
            MessageBoxW(owner_, L"Kiwi is up to date.", L"Kiwi update check",
                MB_OK | MB_ICONINFORMATION);
        }
    }

    void OnDownloadData(CefRefPtr<CefURLRequest> request,
                        const void* data,
                        size_t data_length) override {
        (void)request;
        body_.append(static_cast<const char*>(data), data_length);
    }

    bool GetAuthCredentials(bool is_proxy,
                            const CefString& host,
                            int port,
                            const CefString& realm,
                            const CefString& scheme,
                            CefRefPtr<CefAuthCallback> callback) override {
        (void)is_proxy;
        (void)host;
        (void)port;
        (void)realm;
        (void)scheme;
        (void)callback;
        return false;
    }

    void OnDownloadProgress(CefRefPtr<CefURLRequest> request,
                            int64_t current,
                            int64_t total) override {
        (void)request;
        (void)current;
        (void)total;
    }

    void OnUploadProgress(CefRefPtr<CefURLRequest> request,
                          int64_t current,
                          int64_t total) override {
        (void)request;
        (void)current;
        (void)total;
    }

private:
    HWND owner_;
    std::string body_;

    IMPLEMENT_REFCOUNTING(UpdateRequestClient);
    DISALLOW_COPY_AND_ASSIGN(UpdateRequestClient);
};

}  // namespace

void check_for_updates(HWND owner) {
    CefRequest::HeaderMap headers;
    headers.insert(std::make_pair("User-Agent",
        CefString(std::string("Kiwi/" KIWI_VERSION))));
    CefRefPtr<CefRequest> request = CefRequest::Create();
    request->SetURL("https://api.github.com/repos/mel0k1/Kiwi/releases/latest");
    request->SetMethod("GET");
    request->SetHeaderMap(headers);
    CefURLRequest::Create(request, new UpdateRequestClient(owner),
        CefRequestContext::GetGlobalContext());
}

}  // namespace kiwi
