#include "start_page.hpp"

#include <glib/gstdio.h>

#include <algorithm>
#include <cstdio>
#include <ctime>

namespace kiwi {

bool is_start_page_entry(const std::string& url) {
    std::string trimmed;
    size_t begin = url.find_first_not_of(" \t\r\n");
    if (begin == std::string::npos) return false;
    size_t end = url.find_last_not_of(" \t\r\n");
    trimmed = url.substr(begin, end - begin + 1);
    for (char& c : trimmed) {
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    }
    return trimmed == kStartPageUri || trimmed == "kiwi:start" || trimmed == "kiwi://newtab"
        || trimmed == "about:home" || trimmed == "about:newtab";
}

namespace {

std::string escape_html(const std::string& raw) {
    std::string out;
    out.reserve(raw.size());
    for (char c : raw) {
        switch (c) {
            case '&': out += "&amp;"; break;
            case '<': out += "&lt;"; break;
            case '>': out += "&gt;"; break;
            case '"': out += "&quot;"; break;
            case '\'': out += "&#39;"; break;
            default: out += c;
        }
    }
    return out;
}

std::string escape_js(const std::string& raw) {
    std::string out;
    for (char c : raw) {
        if (c == '\\' || c == '"' || c == '\'') out += '\\';
        if (static_cast<unsigned char>(c) < 0x20) continue;
        out += c;
    }
    return out;
}

std::string escape_url(const std::string& raw) {
    std::string out;
    static const char hex[] = "0123456789ABCDEF";
    for (unsigned char c : raw) {
        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9')
            || c == '-' || c == '_' || c == '.' || c == '~' || c == '/' || c == ':'
            || c == '?' || c == '=' || c == '&' || c == '%' || c == '+') {
            out += static_cast<char>(c);
        } else {
            out += '%';
            out += hex[c >> 4];
            out += hex[c & 0x0F];
        }
    }
    return out;
}

// Search engine template from settings; falls back to DuckDuckGo when the
// stored template lost its %s slot (resolve_entry uses the same shape).
std::string engine_template(const mustela::Settings& settings) {
    for (const mustela::SearchEngine& engine : settings.search_engines) {
        if (!settings.search_default.empty()
            && engine.keyword == settings.search_default
            && engine.url_template.find("%s") != std::string::npos) {
            return engine.url_template;
        }
    }
    if (settings.search_template.find("%s") != std::string::npos) {
        return settings.search_template;
    }
    for (const mustela::SearchEngine& engine : settings.search_engines) {
        if (engine.url_template.find("%s") != std::string::npos) return engine.url_template;
    }
    return "https://duckduckgo.com/?q=%s";
}

// Host part for a tile caption and its favicon path (profile-relative).
std::string host_of(const std::string& url) {
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

const char* base_css() {
    return R"CSS(
:root {
  --surface: #FFF5F0;
  --surface-variant: #FADBD8;
  --primary: #E67E22;
  --primary-soft: #E59866;
  --on-primary: #FFFFFF;
  --outline: #EDBB99;
  --text: #2C3E50;
  --text-dim: #85929E;
  --press: #F5CBA7;
}
* { box-sizing: border-box; margin: 0; padding: 0; }
html, body { height: 100%; }
body {
  background: var(--surface);
  color: var(--text);
  font-family: "Segoe UI", "Noto Sans", Ubuntu, Cantarell, sans-serif;
  display: flex;
  flex-direction: column;
  align-items: center;
  justify-content: center;
  padding: 24px;
}
.brand { display: flex; align-items: center; gap: 12px; margin-bottom: 6px; }
.brand img { width: 44px; height: 44px; }
.brand h1 { font-size: 34px; font-weight: 600; letter-spacing: 0.5px; }
.clock { color: var(--text-dim); font-size: 14px; margin-bottom: 26px; }
.search {
  display: flex; align-items: center; gap: 10px;
  width: min(620px, 100%);
  background: #FFFFFF;
  border: 1px solid var(--outline);
  border-radius: 14px;
  padding: 13px 18px;
}
.search:focus-within { border-color: var(--primary-soft); }
.search svg { flex: none; stroke: var(--text-dim); }
.search input {
  flex: 1; border: none; outline: none; background: transparent;
  font: inherit; font-size: 16px; color: var(--text);
}
.search input::placeholder { color: var(--text-dim); }
.tiles {
  margin-top: 30px;
  display: grid;
  grid-template-columns: repeat(auto-fit, 96px);
  gap: 12px;
  justify-content: center;
  width: min(660px, 100%);
}
.tile {
  display: flex; flex-direction: column; align-items: center; gap: 8px;
  padding: 12px 4px 8px;
  border-radius: 12px;
  border: 1px solid transparent;
  text-decoration: none;
  color: var(--text);
}
.tile:hover { background: var(--surface-variant); border-color: var(--outline); }
.tile .face {
  position: relative;
  width: 40px; height: 40px; border-radius: 10px;
  background: var(--surface-variant);
  border: 1px solid var(--outline);
  display: flex; align-items: center; justify-content: center;
  font-size: 18px; font-weight: 600; color: var(--primary);
  overflow: hidden;
}
.tile .face img {
  position: absolute; top: 0; left: 0; right: 0; bottom: 0;
  margin: auto; width: 24px; height: 24px; background: #FFFFFF;
}
.tile .name {
  max-width: 92px;
  font-size: 12px; color: var(--text-dim);
  white-space: nowrap; overflow: hidden; text-overflow: ellipsis;
}
.banner {
  margin-top: 26px;
  background: var(--surface-variant);
  border: 1px solid var(--outline);
  border-radius: 10px;
  padding: 8px 16px;
  font-size: 13px;
  color: var(--text);
}
.hints { margin-top: 30px; color: var(--text-dim); font-size: 12px; }
.hints b { color: var(--text); font-weight: 600; }
)CSS";
}

std::string search_glyph_svg() {
    return R"SVG(<svg width="18" height="18" viewBox="0 0 16 16" fill="none" stroke-width="1.6" stroke-linecap="round"><circle cx="6.7" cy="6.7" r="3.9"/><path d="M9.8 9.8 L13.6 13.6"/></svg>)SVG";
}

std::string tile_html(const mustela::BookmarkEntry& entry) {
    std::string host = host_of(entry.url);
    if (host.empty()) host = entry.title.empty() ? "link" : entry.title;
    std::string caption = entry.title.empty() ? host : entry.title;
    std::string letter = caption.empty() ? std::string("K") : caption.substr(0, 1);
    std::string face = "<span>" + escape_html(letter) + "</span>";
    if (!host.empty()) {
        face += "<img src=\"favicons/" + escape_url(host) + ".png\" alt=\"\" "
                "onerror=\"this.style.display='none'\">";
    }
    std::string title_attr = escape_html(entry.title.empty() ? entry.url : entry.title);
    std::string href = escape_html(entry.url);
    std::string label = escape_html(caption);
    return "<a class=\"tile\" href=\"" + href + "\" title=\"" + title_attr
        + "\"><span class=\"face\">" + face + "</span><span class=\"name\">" + label
        + "</span></a>";
}

}  // namespace

std::string write_start_page(const std::string& profile_root,
                             const mustela::Settings& settings,
                             const std::vector<mustela::BookmarkEntry>& bookmarks,
                             bool private_mode) {
    std::string html;
    html.reserve(8192);
    html += "<!DOCTYPE html>\n<html><head><meta charset=\"utf-8\">\n"
            "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">\n"
            "<title>Kiwi</title>\n<style>";
    html += base_css();
    html += "</style></head>\n<body>\n";

    html += "<div class=\"brand\"><h1>Kiwi</h1></div>\n";
    html += "<div class=\"clock\" id=\"clock\"></div>\n";

    std::string template_js = escape_js(engine_template(settings));
    html += "<form class=\"search\" id=\"go\">\n";
    html += search_glyph_svg();
    html += "<input id=\"q\" type=\"text\" autocomplete=\"off\" autofocus "
            "spellcheck=\"false\" placeholder=\"Search or enter address\">\n";
    html += "</form>\n";

    std::vector<const mustela::BookmarkEntry*> tiles;
    for (const mustela::BookmarkEntry& entry : bookmarks) {
        if (!entry.url.empty()) tiles.push_back(&entry);
        if (tiles.size() >= 12) break;
    }
    if (!tiles.empty()) {
        html += "<div class=\"tiles\">\n";
        for (const mustela::BookmarkEntry* entry : tiles) html += tile_html(*entry) + "\n";
        html += "</div>\n";
    }

    if (private_mode) {
        html += "<div class=\"banner\">You are browsing privately — history and "
                "site data are not stored.</div>\n";
    } else {
        html += "<div class=\"hints\"><b>Ctrl+T</b> new tab&nbsp;&nbsp; "
                "<b>Ctrl+L</b> address bar&nbsp;&nbsp; <b>Ctrl+D</b> bookmark&nbsp;&nbsp; "
                "<b>Ctrl+,</b> settings</div>\n";
    }

    html += "<script>\n";
    html += "var ENGINE = \"" + template_js + "\";\n";
    html += R"JS(
function clockText() {
  var now = new Date();
  var days = ["Sunday","Monday","Tuesday","Wednesday","Thursday","Friday","Saturday"];
  var months = ["January","February","March","April","May","June","July",
                "August","September","October","November","December"];
  function pad(n) { return (n < 10 ? "0" : "") + n; }
  return days[now.getDay()] + ", " + months[now.getMonth()] + " " + now.getDate()
      + " — " + pad(now.getHours()) + ":" + pad(now.getMinutes());
}
document.getElementById("clock").textContent = clockText();
setInterval(function() {
  document.getElementById("clock").textContent = clockText();
}, 30000);
function looksLikeUrl(text) {
  if (/\s/.test(text)) return false;
  if (/^https?:\/\//i.test(text) || /^file:/i.test(text)) return true;
  return /^[^\s]+\.[a-zA-Z]{2,}(:\d+)?(\/.*)?$/.test(text);
}
document.getElementById("go").addEventListener("submit", function(event) {
  event.preventDefault();
  var value = document.getElementById("q").value.trim();
  if (!value) return;
  if (looksLikeUrl(value)) {
    if (!/^[a-zA-Z][a-zA-Z0-9+.-]*:/.test(value)) value = "https://" + value;
    location.href = value;
  } else {
    location.href = ENGINE.replace("%s", encodeURIComponent(value));
  }
});
)JS";
    html += "</script>\n</body></html>\n";

    std::string path = profile_root;
    if (!path.empty() && path.back() != '/' && path.back() != '\\') path += "/";
    path += "start.html";
    g_mkdir_with_parents(profile_root.c_str(), 0700);
    FILE* file = g_fopen(path.c_str(), "wb");
    if (!file) return std::string();
    size_t written = fwrite(html.data(), 1, html.size(), file);
    fclose(file);
    if (written != html.size()) return std::string();

    // file URI with forward slashes; percent-encode spaces for GLib/Webkit.
    char* uri = g_filename_to_uri(path.c_str(), nullptr, nullptr);
    std::string out = uri ? uri : std::string();
    g_free(uri);
    return out;
}

}
