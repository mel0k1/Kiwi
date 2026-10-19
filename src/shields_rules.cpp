#include "shields_rules.hpp"

#include <cctype>
#include <fstream>

namespace kiwi {
namespace shields {

namespace {

// Strips the easylist decoration off a rule line: "||domain^", any trailing
// "^$option..." and a leading "@@" exception marker (exceptions are dropped,
// not honored — the vendored list is curated to not need them).
std::string strip_rule_syntax(const std::string& line) {
    std::string text = line;
    if (text.rfind("@@", 0) == 0) return std::string();
    if (text.rfind("||", 0) == 0) text.erase(0, 2);
    size_t cut = text.find_first_of("^$");
    if (cut != std::string::npos) text.resize(cut);
    return text;
}

}  // namespace

std::string normalize_host(const std::string& host_or_url) {
    std::string text = host_or_url;
    // If a full URL slipped in, cut to the authority first.
    size_t scheme = text.find("://");
    if (scheme != std::string::npos) text.erase(0, scheme + 3);
    size_t tail = text.find_first_of("/?#");
    if (tail != std::string::npos) text.resize(tail);
    size_t at = text.find('@');
    if (at != std::string::npos) text.erase(0, at + 1);
    size_t colon = text.find(':');
    if (colon != std::string::npos) text.resize(colon);
    size_t begin = text.find_first_not_of(" \t\r\n");
    if (begin == std::string::npos) return std::string();
    size_t end = text.find_last_not_of(" \t\r\n");
    std::string out;
    out.reserve(end - begin + 1);
    for (size_t i = begin; i <= end; ++i) {
        unsigned char c = static_cast<unsigned char>(text[i]);
        if (c >= 'A' && c <= 'Z') c = static_cast<unsigned char>(c - 'A' + 'a');
        out.push_back(static_cast<char>(c));
    }
    while (!out.empty() && out.front() == '.') out.erase(out.begin());
    while (!out.empty() && out.back() == '.') out.pop_back();
    return out;
}

bool valid_domain(const std::string& domain) {
    if (domain.empty() || domain.size() > 253) return false;
    if (domain.front() == '.' || domain.back() == '.') return false;
    if (domain.front() == '-' || domain.back() == '-') return false;
    if (domain.find("..") != std::string::npos) return false;
    for (char c : domain) {
        unsigned char ch = static_cast<unsigned char>(c);
        bool ok = ch == '.' || ch == '-' || (ch >= '0' && ch <= '9')
            || (ch >= 'a' && ch <= 'z');
        if (!ok) return false;
    }
    return true;
}

void Rules::add_domain(const std::string& domain) {
    std::string clean = normalize_host(domain);
    if (!valid_domain(clean)) return;
    domains_.insert(clean);
}

size_t Rules::load_file(const std::string& path) {
    std::ifstream in(path);
    if (!in) return 0;
    size_t parsed = 0;
    std::string line;
    while (std::getline(in, line)) {
        size_t begin = line.find_first_not_of(" \t\r\n");
        if (begin == std::string::npos) continue;
        size_t end = line.find_last_not_of(" \t\r\n");
        std::string value = line.substr(begin, end - begin + 1);
        if (value.front() == '#') continue;
        // Hosts-file form: "0.0.0.0 ads.example.com" / "127.0.0.1 ads...".
        size_t space = value.find_first_of(" \t");
        if (space != std::string::npos) {
            std::string first = value.substr(0, space);
            if (first == "0.0.0.0" || first == "127.0.0.1" || first == "::1") {
                size_t host_begin = value.find_first_not_of(" \t", space);
                if (host_begin == std::string::npos) continue;
                value = value.substr(host_begin);
            }
        }
        value = strip_rule_syntax(value);
        value = normalize_host(value);
        if (!valid_domain(value)) continue;
        size_t before = domains_.size();
        domains_.insert(value);
        if (domains_.size() != before) ++parsed;
    }
    return parsed;
}

bool Rules::hit(const std::string& host) const {
    std::string clean = normalize_host(host);
    if (clean.empty()) return false;
    while (true) {
        if (domains_.count(clean)) return true;
        size_t dot = clean.find('.');
        if (dot == std::string::npos) return false;
        clean.erase(0, dot + 1);
    }
}

}

}
