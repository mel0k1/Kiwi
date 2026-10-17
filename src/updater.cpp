#include "updater.hpp"

#include "browser.hpp"

#include <libsoup/soup.h>

#include <cctype>
#include <cstdint>
#include <fstream>
#include <thread>
#include <vector>

namespace kiwi {

namespace {

UpdateInfo g_updates;

bool ends_with(const std::string& text, const std::string& suffix) {
    return text.size() >= suffix.size()
        && text.compare(text.size() - suffix.size(), suffix.size(), suffix) == 0;
}

std::string to_lower_copy(const std::string& in) {
    std::string out = in;
    for (char& c : out) {
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    }
    return out;
}

// ---------------- minimal JSON extraction ----------------

void append_utf8(std::string& value, unsigned code) {
    if (code < 0x80) {
        value.push_back(static_cast<char>(code));
    } else if (code < 0x800) {
        value.push_back(static_cast<char>(0xC0 | (code >> 6)));
        value.push_back(static_cast<char>(0x80 | (code & 0x3F)));
    } else {
        value.push_back(static_cast<char>(0xE0 | (code >> 12)));
        value.push_back(static_cast<char>(0x80 | ((code >> 6) & 0x3F)));
        value.push_back(static_cast<char>(0x80 | (code & 0x3F)));
    }
}

// Finds the string value for "key" at or after `from`; strings are unescaped.
bool json_find_string(const std::string& json, size_t from, const std::string& key,
    std::string& out, size_t& next) {
    std::string needle = "\"" + key + "\"";
    size_t at = json.find(needle, from);
    if (at == std::string::npos) return false;
    size_t colon = json.find(':', at + needle.size());
    if (colon == std::string::npos) return false;
    size_t quote = json.find('"', colon + 1);
    if (quote == std::string::npos) return false;

    std::string value;
    size_t i = quote + 1;
    while (i < json.size()) {
        char c = json[i];
        if (c == '"') {
            out = std::move(value);
            next = i + 1;
            return true;
        }
        if (c == '\\') {
            ++i;
            if (i >= json.size()) return false;
            char esc = json[i];
            switch (esc) {
                case '"': value.push_back('"'); break;
                case '\\': value.push_back('\\'); break;
                case '/': value.push_back('/'); break;
                case 'n': value.push_back('\n'); break;
                case 'r': value.push_back('\r'); break;
                case 't': value.push_back('\t'); break;
                case 'b': value.push_back('\b'); break;
                case 'f': value.push_back('\f'); break;
                case 'u': {
                    if (i + 4 >= json.size()) return false;
                    unsigned code = 0;
                    for (int k = 1; k <= 4; ++k) {
                        char h = json[i + k];
                        unsigned digit;
                        if (h >= '0' && h <= '9') digit = static_cast<unsigned>(h - '0');
                        else if (h >= 'a' && h <= 'f') digit = static_cast<unsigned>(h - 'a' + 10);
                        else if (h >= 'A' && h <= 'F') digit = static_cast<unsigned>(h - 'A' + 10);
                        else return false;
                        code = (code << 4) | digit;
                    }
                    append_utf8(value, code);
                    i += 4;
                    break;
                }
                default:
                    return false;
            }
            ++i;
            continue;
        }
        if (static_cast<unsigned char>(c) < 0x20) return false;
        value.push_back(c);
        ++i;
    }
    return false;
}

// Yields (name, url) pairs for every asset object after `from`.
size_t json_next_asset(const std::string& json, size_t from, std::string& name,
    std::string& url, size_t& next) {
    size_t object = json.find('{', from);
    if (object == std::string::npos) return 0;
    size_t depth = 0;
    size_t end = object;
    for (size_t i = object; i < json.size(); ++i) {
        if (json[i] == '{') ++depth;
        if (json[i] == '}') {
            --depth;
            if (depth == 0) {
                end = i;
                break;
            }
        }
    }
    if (depth != 0) return 0;
    size_t cursor = object;
    std::string got_name;
    std::string got_url;
    if (!json_find_string(json, cursor, "name", got_name, cursor)) return 0;
    if (!json_find_string(json, cursor, "browser_download_url", got_url, cursor)) return 0;
    name = std::move(got_name);
    url = std::move(got_url);
    next = end + 1;
    return 1;
}

// ---------------- version comparison ----------------

std::vector<int> version_parts(const std::string& text) {
    std::string clean = to_lower_copy(text);
    size_t begin = clean.find_first_of("0123456789");
    if (begin == std::string::npos) return {};
    std::vector<int> parts;
    size_t i = begin;
    while (i < clean.size()) {
        if (std::isdigit(static_cast<unsigned char>(clean[i]))) {
            int value = 0;
            while (i < clean.size() && std::isdigit(static_cast<unsigned char>(clean[i]))) {
                value = value * 10 + (clean[i] - '0');
                ++i;
            }
            parts.push_back(value);
        } else if (clean[i] == '.') {
            ++i;
        } else {
            break;
        }
    }
    return parts;
}

bool version_is_newer(const std::string& candidate, const std::string& current) {
    std::vector<int> a = version_parts(candidate);
    std::vector<int> b = version_parts(current);
    size_t width = a.size() > b.size() ? a.size() : b.size();
    for (size_t i = 0; i < width; ++i) {
        int left = i < a.size() ? a[i] : 0;
        int right = i < b.size() ? b[i] : 0;
        if (left != right) return left > right;
    }
    return false;
}

// ---------------- platform asset pick ----------------

int asset_score(const std::string& name) {
    std::string low = to_lower_copy(name);
#ifdef _WIN32
    if (ends_with(low, ".exe")) return 4;
    if (ends_with(low, ".zip")) return 2;
    return 0;
#else
    if (!ends_with(low, ".deb")) return 0;
    if (low.find("amd64") != std::string::npos || low.find("x86_64") != std::string::npos) return 4;
    if (low.find("all") != std::string::npos) return 3;
    return 2;
#endif
}

// ---------------- HTTP helpers (libsoup, synchronous) ----------------

std::string kiwi_user_agent() {
    return "Kiwi/" KIWI_VERSION " (releases updater; +https://github.com/mel0k1/Kiwi)";
}

bool http_get(const std::string& url, std::string& out, std::string& error) {
    SoupSession* session = soup_session_new();
    SoupMessage* msg = soup_message_new("GET", url.c_str());
    if (!msg) {
        error = "invalid release URL";
        g_object_unref(session);
        return false;
    }
    SoupMessageHeaders* headers = soup_message_get_request_headers(msg);
    soup_message_headers_append(headers, "User-Agent", kiwi_user_agent().c_str());
    soup_message_headers_append(headers, "Accept", "application/vnd.github+json");
    GError* gerror = nullptr;
    GInputStream* stream = soup_session_send(session, msg, nullptr, &gerror);
    bool ok = false;
    if (!stream) {
        error = gerror ? gerror->message : "connection failed";
        if (gerror) g_error_free(gerror);
    } else if (!SOUP_STATUS_IS_SUCCESSFUL(soup_message_get_status(msg))) {
        error = "HTTP " + std::to_string(soup_message_get_status(msg));
    } else {
        char buffer[8192];
        gssize got = 0;
        while ((got = g_input_stream_read(stream, buffer, sizeof(buffer), nullptr, nullptr)) > 0) {
            out.append(buffer, static_cast<size_t>(got));
            if (out.size() > 8u * 1024 * 1024) break;
        }
        ok = got == 0 && !out.empty();
        if (!ok) error = "empty release response";
    }
    if (stream) g_object_unref(stream);
    g_object_unref(msg);
    g_object_unref(session);
    return ok;
}

std::string kiwi_downloads_dir() {
    const gchar* special = g_get_user_special_dir(G_USER_DIRECTORY_DOWNLOAD);
    if (special && *special) return special;
    const gchar* home = g_get_home_dir();
    return home ? std::string(home) + "/Downloads" : std::string(".");
}

std::string unique_download_path(const std::string& dir, const std::string& name) {
    std::string path = dir + "/" + name;
    if (!g_file_test(path.c_str(), G_FILE_TEST_EXISTS)) return path;
    std::string base = name;
    std::string ext;
    size_t dot = name.rfind('.');
    if (dot != std::string::npos && dot != 0) {
        base = name.substr(0, dot);
        ext = name.substr(dot);
    }
    for (int n = 2; n < 1000; ++n) {
        std::string candidate = dir + "/" + base + " (" + std::to_string(n) + ")" + ext;
        if (!g_file_test(candidate.c_str(), G_FILE_TEST_EXISTS)) return candidate;
    }
    return path;
}

bool http_download(const std::string& url, const std::string& dest, int& percent,
    std::string& error) {
    SoupSession* session = soup_session_new();
    SoupMessage* msg = soup_message_new("GET", url.c_str());
    if (!msg) {
        error = "invalid download URL";
        g_object_unref(session);
        return false;
    }
    SoupMessageHeaders* headers = soup_message_get_request_headers(msg);
    soup_message_headers_append(headers, "User-Agent", kiwi_user_agent().c_str());
    GError* gerror = nullptr;
    GInputStream* stream = soup_session_send(session, msg, nullptr, &gerror);
    bool ok = false;
    if (!stream) {
        error = gerror ? gerror->message : "connection failed";
        if (gerror) g_error_free(gerror);
    } else if (!SOUP_STATUS_IS_SUCCESSFUL(soup_message_get_status(msg))) {
        error = "HTTP " + std::to_string(soup_message_get_status(msg));
    } else {
        goffset total = soup_message_headers_get_content_length(
            soup_message_get_response_headers(msg));
        GFile* file = g_file_new_for_path(dest.c_str());
        GFileOutputStream* out = g_file_create(file, G_FILE_CREATE_REPLACE_DESTINATION,
            nullptr, &gerror);
        if (!out) {
            error = gerror ? gerror->message : "cannot create file";
            if (gerror) g_error_free(gerror);
        } else {
            char buffer[65536];
            gssize got = 0;
            std::uint64_t received = 0;
            ok = true;
            while ((got = g_input_stream_read(stream, buffer, sizeof(buffer), nullptr, nullptr)) > 0) {
                gssize written = g_output_stream_write(G_OUTPUT_STREAM(out), buffer,
                    static_cast<gsize>(got), nullptr, nullptr);
                if (written < got) {
                    ok = false;
                    error = "write failed";
                    break;
                }
                received += static_cast<std::uint64_t>(got);
                if (total > 0) {
                    percent = static_cast<int>(received * 100.0 / static_cast<double>(total));
                    if (percent > 100) percent = 100;
                }
            }
            if (ok && got < 0) {
                ok = false;
                error = "download interrupted";
            }
            if (ok && received == 0) {
                ok = false;
                error = "empty download";
            }
            if (!g_output_stream_close(G_OUTPUT_STREAM(out), nullptr, nullptr) && ok) {
                ok = false;
                error = "close failed";
            }
            g_object_unref(out);
        }
        g_object_unref(file);
    }
    if (stream) g_object_unref(stream);
    g_object_unref(msg);
    g_object_unref(session);
    return ok;
}

// ---------------- worker payloads ----------------

struct CheckResult {
    Browser* browser = nullptr;
    bool ok = false;
    bool newer = false;
    std::string tag;
    std::string body;
    std::string release_url;
    std::string asset_url;
    std::string asset_name;
    std::string error;
};

gboolean check_done_idle(gpointer data) {
    CheckResult* result = static_cast<CheckResult*>(data);
    UpdateInfo& u = updates();
    u.checking = false;
    if (result->ok) {
        u.error.clear();
        u.latest_version = result->tag;
        u.notes = result->body;
        u.release_url = result->release_url;
        u.asset_url = result->asset_url;
        u.asset_name = result->asset_name;
        u.up_to_date = !result->newer;
        u.available = result->newer && !result->asset_url.empty();
        if (result->newer && result->asset_url.empty()) {
            u.error = "release " + result->tag + " has no package for this platform yet";
        }
    } else {
        u.error = result->error.empty() ? "release check failed" : result->error;
        u.available = false;
        u.up_to_date = false;
    }
    if (result->browser) updates_refresh_ui(result->browser);
    delete result;
    return G_SOURCE_REMOVE;
}

struct DownloadResult {
    Browser* browser = nullptr;
    bool ok = false;
    std::string path;
    std::string error;
};

gboolean download_done_idle(gpointer data) {
    DownloadResult* result = static_cast<DownloadResult*>(data);
    UpdateInfo& u = updates();
    u.busy = false;
    if (u.poll_id) {
        g_source_remove(u.poll_id);
        u.poll_id = 0;
    }
    u.percent = result->ok ? 100 : 0;
    if (result->ok) {
        u.error.clear();
        u.downloaded_path = result->path;
        if (result->browser) flash_status(*result->browser, "update downloaded: " + result->path);
    } else {
        u.error = result->error.empty() ? "download failed" : result->error;
    }
    if (result->browser) updates_refresh_ui(result->browser);
    delete result;
    return G_SOURCE_REMOVE;
}

gboolean download_poll_tick(gpointer) {
    UpdateInfo& u = updates();
    if (u.busy && updates_ui_bound()) updates_refresh_ui(nullptr);
    return G_SOURCE_CONTINUE;
}

std::string extract_github_error(const std::string& body) {
    std::string message;
    size_t next = 0;
    if (json_find_string(body, 0, "message", message, next)) return message;
    return body.substr(0, 160);
}

void check_worker(CheckResult* result) {
    std::string json;
    std::string error;
    if (!http_get("https://api.github.com/repos/mel0k1/Kiwi/releases/latest", json, error)) {
        result->ok = false;
        result->error = error;
        g_idle_add(check_done_idle, result);
        return;
    }
    if (json.find("\"tag_name\"") == std::string::npos) {
        result->ok = false;
        result->error = "no releases yet: " + extract_github_error(json);
        g_idle_add(check_done_idle, result);
        return;
    }
    size_t next = 0;
    std::string tag;
    std::string body;
    std::string html;
    if (!json_find_string(json, 0, "tag_name", tag, next)
        || !json_find_string(json, next, "body", body, next)
        || !json_find_string(json, next, "html_url", html, next)) {
        result->ok = false;
        result->error = "release payload looks malformed";
        g_idle_add(check_done_idle, result);
        return;
    }
    size_t assets_at = json.find("\"assets\"");
    size_t cursor = assets_at == std::string::npos ? std::string::npos : assets_at;
    std::string best_name;
    std::string best_url;
    int best_score = 0;
    while (cursor != std::string::npos) {
        std::string name;
        std::string url;
        size_t after = 0;
        if (!json_next_asset(json, cursor, name, url, after)) break;
        int score = asset_score(name);
        if (score > best_score) {
            best_score = score;
            best_name = name;
            best_url = url;
        }
        cursor = after;
    }
    result->ok = true;
    result->newer = version_is_newer(tag, KIWI_VERSION);
    result->tag = tag;
    result->body = body;
    result->release_url = html;
    result->asset_url = best_url;
    result->asset_name = best_name;
    g_idle_add(check_done_idle, result);
}

void download_worker(DownloadResult* result, std::string url, std::string name) {
    std::string dest = unique_download_path(kiwi_downloads_dir(), name);
    std::string error;
    int percent = 0;
    result->ok = http_download(url, dest, percent, error);
    result->path = dest;
    result->error = error;
    g_idle_add(download_done_idle, result);
}

}

UpdateInfo& updates() {
    return g_updates;
}

void updates_check(Browser* b) {
    UpdateInfo& u = updates();
    if (u.checking || u.busy) return;
    u.checking = true;
    u.error.clear();
    u.available = false;
    u.up_to_date = false;
    updates_refresh_ui(b);

    CheckResult* result = new CheckResult();
    result->browser = b;
    std::thread(check_worker, result).detach();
}

void updates_download(Browser* b) {
    UpdateInfo& u = updates();
    if (u.busy || u.checking || u.asset_url.empty()) return;
    u.busy = true;
    u.error.clear();
    u.percent = 0;
    u.downloaded_path.clear();
    u.poll_id = g_timeout_add(250, download_poll_tick, nullptr);
    updates_refresh_ui(b);

    DownloadResult* result = new DownloadResult();
    result->browser = b;
    std::string url = u.asset_url;
    std::string name = u.asset_name.empty() ? "kiwi-update" : u.asset_name;
    std::thread(download_worker, result, url, name).detach();
}

void updates_open_folder(Browser*) {
    UpdateInfo& u = updates();
    if (u.downloaded_path.empty()) return;
    size_t slash = u.downloaded_path.rfind('/');
    std::string dir = slash == std::string::npos ? u.downloaded_path
        : u.downloaded_path.substr(0, slash);
    std::string uri = "file://" + dir;
    g_app_info_launch_default_for_uri(uri.c_str(), nullptr, nullptr);
}

void updates_shutdown() {
    UpdateInfo& u = updates();
    if (u.poll_id) {
        g_source_remove(u.poll_id);
        u.poll_id = 0;
    }
}

}
