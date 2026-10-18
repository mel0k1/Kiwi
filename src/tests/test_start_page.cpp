// Local check for the Kiwi start page generator: renders a page with a
// default engine, XSS-prone bookmark titles and an empty engine list, then
// asserts the shape of the HTML and the kiwi://start matcher.
#include <cassert>
#include <cstdio>
#include <fstream>
#include <sstream>

#include "start_page.hpp"

int main() {
    mustela::Settings settings;
    std::vector<mustela::BookmarkEntry> bookmarks = {
        {"https://example.org", "Example <script>alert(1)</script>"},
        {"https://duckduckgo.com", "DDG"},
        {"weird", ""}};

    // 1. Default engine template
    std::string uri = kiwi::write_start_page("/tmp/kiwi-start-check", settings, bookmarks, false);
    assert(!uri.empty() && uri.rfind("file://", 0) == 0);
    std::ifstream in("/tmp/kiwi-start-check/start.html");
    std::stringstream buffer;
    buffer << in.rdbuf();
    std::string html = buffer.str();
    assert(html.find("duckduckgo.com") != std::string::npos);
    assert(html.find("&lt;script&gt;") != std::string::npos);   // titles escaped
    assert(html.find("<script>alert") == std::string::npos);    // no raw injection
    assert(html.find("ENGINE = \"https://duckduckgo.com/?q=%s\"") != std::string::npos);
    assert(html.find("favicons/duckduckgo.com.png") != std::string::npos);
    assert(html.find("https://example.org") != std::string::npos);

    // 2. Custom engine wins when marked default
    mustela::Settings custom;
    custom.search_default = "wiki";
    custom.search_engines.push_back({"wiki", "Wikipedia", "https://ru.wikipedia.org/w/index.php?search=%s"});
    custom.search_template = "https://duckduckgo.com/?q=%s";
    std::string uri2 = kiwi::write_start_page("/tmp/kiwi-start-check", custom, {}, false);
    assert(!uri2.empty());
    std::ifstream in2("/tmp/kiwi-start-check/start.html");
    std::stringstream b2;
    b2 << in2.rdbuf();
    assert(b2.str().find("wikipedia.org") != std::string::npos);

    // 3. Broken templates fall back to DuckDuckGo
    mustela::Settings broken;
    broken.search_template = "https://example.com/search";   // no %s slot
    broken.search_engines.push_back({"x", "X", "no-slot-here"});
    std::string uri3 = kiwi::write_start_page("/tmp/kiwi-start-check", broken, {}, false);
    assert(!uri3.empty());
    std::ifstream in3("/tmp/kiwi-start-check/start.html");
    std::stringstream b3;
    b3 << in3.rdbuf();
    assert(b3.str().find("duckduckgo.com") != std::string::npos);

    // 4. Start page matcher
    assert(kiwi::is_start_page_entry("kiwi://start"));
    assert(kiwi::is_start_page_entry("KIWI://NewTab"));
    assert(kiwi::is_start_page_entry("about:home"));
    assert(!kiwi::is_start_page_entry("https://example.org"));
    assert(!kiwi::is_start_page_entry(""));

    std::printf("start-page checks passed\n");
    return 0;
}
