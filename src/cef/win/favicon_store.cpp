#include "favicon_store.hpp"

#include "gfx_win.hpp"

#include "include/cef_request.h"
#include "include/cef_request_context.h"
#include "include/cef_urlrequest.h"

#include <direct.h>

#include <algorithm>
#include <map>
#include <mutex>
#include <vector>

namespace kiwi {
namespace favicons {

namespace {

constexpr std::size_t kMaxBytes = 2u << 20;
constexpr int kCachePx = 32;

std::string g_profile_root;
std::function<void()> g_notify;

struct Entry {
    bool loading = false;
    bool fetched = false;
    bool disk_checked = false;
    std::size_t pending = 0;
    std::vector<std::string> candidates;
    std::vector<unsigned char> bytes;
    void* image = nullptr;
    void* cached = nullptr;
};

std::mutex g_mutex;
std::map<std::string, Entry> g_entries;

std::string lower_ascii(std::string text) {
    for (char& c : text) {
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    }
    return text;
}

std::string host_from_url(const std::string& url) {
    size_t scheme = url.find("://");
    std::string rest = scheme == std::string::npos ? url : url.substr(scheme + 3);
    size_t slash = rest.find_first_of("/?#");
    if (slash != std::string::npos) rest.resize(slash);
    size_t at = rest.find('@');
    if (at != std::string::npos) rest = rest.substr(at + 1);
    size_t colon = rest.find(':');
    if (colon != std::string::npos) rest.resize(colon);
    return lower_ascii(rest);
}

std::string cache_dir() {
    std::string dir = g_profile_root;
    if (!dir.empty() && dir.back() != '/' && dir.back() != '\\') dir += '/';
    return dir + "favicons";
}

std::string cache_path(const std::string& host) {
    std::string safe;
    for (char c : host) {
        bool ok = (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9')
            || c == '-' || c == '.' || c == '_';
        if (ok) safe += c;
    }
    if (safe.empty()) return std::string();
    return cache_dir() + "/" + safe + ".png";
}

void store_png_on_disk(const std::string& host, void* image) {
    std::string path = cache_path(host);
    if (path.empty() || !image) return;
    std::string dir = cache_dir();
    _mkdir(dir.c_str());
    gfx::save_image_png(image, path);
}

void* load_from_disk(const std::string& host) {
    std::string path = cache_path(host);
    if (path.empty()) return nullptr;
    return gfx::load_image_file(path);
}

void store_png_on_disk(const std::string& host, void* image);
void* load_from_disk(const std::string& host);

void start_next(const std::string& host, Entry& entry);

class IconClient : public CefURLRequestClient {
public:
    IconClient(std::string host) : host_(std::move(host)) {}

    void OnRequestComplete(CefRefPtr<CefURLRequest> request) override {
        Entry* entry = locked_entry();
        if (!entry) return;
        bool ok = request->GetRequestStatus() == UR_SUCCESS
            && !entry->bytes.empty();
        finish(ok);
    }

private:
    void OnUploadProgress(CefRefPtr<CefURLRequest>, std::int64_t,
                          std::int64_t) override {}

    void OnDownloadProgress(CefRefPtr<CefURLRequest>, std::int64_t,
                            std::int64_t) override {}

    void OnDownloadData(CefRefPtr<CefURLRequest>, const void* data,
                        std::size_t size) override {
        Entry* entry = locked_entry();
        if (!entry || entry->bytes.size() + size > kMaxBytes) return;
        const unsigned char* bytes = static_cast<const unsigned char*>(data);
        entry->bytes.insert(entry->bytes.end(), bytes, bytes + size);
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

    Entry* locked_entry() {
        g_mutex.lock();
        auto it = g_entries.find(host_);
        if (it == g_entries.end()) {
            g_mutex.unlock();
            return nullptr;
        }
        return &it->second;
    }

    // Must be called with the entry mutex held by locked_entry().
    void finish(bool ok) {
        std::string host = host_;
        std::vector<unsigned char> bytes;
        g_mutex.lock();
        Entry& entry = g_entries[host];
        if (ok) bytes = entry.bytes;
        entry.loading = false;
        bool retry = !ok && entry.pending < entry.candidates.size();
        if (ok || !retry) entry.fetched = true;
        g_mutex.unlock();

        if (ok) {
            void* image = gfx::decode_image_bytes(bytes.data(), bytes.size());
            if (image) {
                void* cached = gfx::resize_image(image, kCachePx);
                g_mutex.lock();
                g_entries[host].image = image;
                g_entries[host].cached = cached ? cached : image;
                g_mutex.unlock();
                store_png_on_disk(host, cached ? cached : image);
            }
        }
        if (retry) {
            g_mutex.lock();
            auto it = g_entries.find(host);
            if (it != g_entries.end()) start_next(host, it->second);
            g_mutex.unlock();
        }
        if (g_notify) g_notify();
    }

    std::string host_;
    IMPLEMENT_REFCOUNTING(IconClient);
    DISALLOW_COPY_AND_ASSIGN(IconClient);
};

void start_next(const std::string& host, Entry& entry) {
    while (entry.pending < entry.candidates.size()) {
        const std::string url = entry.candidates[entry.pending++];
        if (url.rfind("http://", 0) != 0 && url.rfind("https://", 0) != 0) continue;
        entry.loading = true;
        entry.bytes.clear();
        CefRefPtr<CefRequest> request = CefRequest::Create();
        request->SetURL(url);
        request->SetMethod("GET");
        CefURLRequest::Create(request, new IconClient(host),
            CefRequestContext::GetGlobalContext());
        return;
    }
    entry.loading = false;
    entry.fetched = true;
}

void sort_candidates(std::vector<std::string>& urls) {
    auto score = [](const std::string& url) {
        std::string low = lower_ascii(url);
        bool png = low.rfind(".png") == low.size() - 4;
        bool ico = low.rfind(".ico") == low.size() - 4;
        if (png) return 0;
        if (ico) return 1;
        return 2;
    };
    std::stable_sort(urls.begin(), urls.end(),
        [score](const std::string& a, const std::string& b) {
            return score(a) < score(b);
        });
}

}  // namespace

void set_notifier(std::function<void()> notify) {
    g_notify = std::move(notify);
}

void set_profile_root(const std::string& profile_root) {
    g_profile_root = profile_root;
}

void update_urls(const std::string& host, const std::vector<std::string>& urls) {
    if (host.empty() || urls.empty()) return;
    g_mutex.lock();
    Entry& entry = g_entries[host];
    if (entry.loading || entry.fetched) {
        g_mutex.unlock();
        return;
    }
    entry.candidates = urls;
    sort_candidates(entry.candidates);
    start_next(host, entry);
    g_mutex.unlock();
    if (g_notify) g_notify();
}

void* bitmap(const std::string& host) {
    if (host.empty()) return nullptr;
    g_mutex.lock();
    Entry& entry = g_entries[host];
    if (entry.cached) {
        g_mutex.unlock();
        return entry.cached;
    }
    if (entry.loading || entry.fetched || entry.disk_checked) {
        g_mutex.unlock();
        return nullptr;
    }
    entry.disk_checked = true;
    g_mutex.unlock();

    void* disk = load_from_disk(host);
    if (!disk) return nullptr;
    void* cached = gfx::resize_image(disk, kCachePx);
    g_mutex.lock();
    g_entries[host].image = disk;
    g_entries[host].cached = cached ? cached : disk;
    g_mutex.unlock();
    return g_entries[host].cached;
}

}
}
