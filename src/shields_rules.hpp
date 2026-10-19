#pragma once

#include <string>

#include <unordered_set>

// Kiwi Shields — the network ad/tracker matcher behind the browser. This is
// the hot path: OnBeforeResourceLoad calls hit() for every request, so the
// store is a flat hash set and hit() walks the host's labels ("a.b.c" ->
// "b.c" -> "c"), giving O(labels) hash lookups per request instead of the
// linear scan of mustela::Blocklist. The GTK build never calls this; the
// class is kept portable so the unit tests run everywhere.

namespace kiwi {
namespace shields {

class Rules {
public:
    // Adds one blocked domain ("doubleclick.net"). Subdomains match
    // implicitly through the label walk in hit().
    void add_domain(const std::string& domain);

    // Loads domains from |path|: one entry per line, tolerating bare
    // domains, hosts-file lines ("0.0.0.0 ads.example.com") and easylist
    // network lines ("||ads.example.com^" plus $options, which are ignored).
    // '#' comments and blank lines are skipped. Returns the parsed count.
    size_t load_file(const std::string& path);

    // True when |host| equals or is a subdomain of any blocked domain.
    bool hit(const std::string& host) const;

    size_t size() const { return domains_.size(); }
    void clear() { domains_.clear(); }

private:
    std::unordered_set<std::string> domains_;
};

// Lowercases, trims and strips a URL down to its host if a URL was passed.
std::string normalize_host(const std::string& host_or_url);

// True when the string is a plausible domain ([a-z0-9.-] only, no empty
// labels, no leading/trailing dot or dash).
bool valid_domain(const std::string& domain);

}

}
