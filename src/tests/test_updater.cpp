#include "../src/updater.cpp"

#include <cstdio>
#include <string>

namespace kiwi {

bool updates_ui_bound() {
    return false;
}

void updates_refresh_ui(Browser*) {}

}

using namespace kiwi;
using kiwi::updates;

void flash_status(Browser&, const std::string&) {}
int failures = 0;
int checks_total = 0;

void check(bool ok, const char* what) {
    ++checks_total;
    if (!ok) {
        std::fprintf(stderr, "FAIL: %s\n", what);
        ++failures;
    }
}

const char* release_json =
    "{"
    "\"url\":\"https://api.github.com/repos/mel0k1/Kiwi/releases/1\","
    "\"html_url\":\"https://github.com/mel0k1/Kiwi/releases/tag/v0.6.0\","
    "\"tag_name\":\"v0.6.0\","
    "\"name\":\"Kiwi 0.6.0\","
    "\"body\":\"Check-for-updates button\\nand packaging with \\\"quotes\\\" and\\\\slashes\","
    "\"assets\":["
    "{\"url\":\"https://api.github.com/assets/1\","
    "\"name\":\"kiwi_0.6.0_amd64.deb\","
    "\"browser_download_url\":\"https://github.com/mel0k1/Kiwi/releases/download/v0.6.0/kiwi_0.6.0_amd64.deb\","
    "\"size\":123},"
    "{\"url\":\"https://api.github.com/assets/2\","
    "\"name\":\"source.tar.gz\","
    "\"browser_download_url\":\"https://github.com/mel0k1/Kiwi/archive/source.tar.gz\","
    "\"size\":456}"
    "]"
    "}";

int main() {
    std::string tag;
    std::string body;
    std::string html;
    size_t next = 0;
    check(json_find_string(release_json, 0, "tag_name", tag, next), "tag_name found");
    check(tag == "v0.6.0", "tag_name value");
    check(json_find_string(release_json, next, "body", body, next), "body found");
    check(body == "Check-for-updates button\nand packaging with \"quotes\" and\\slashes",
        "body unescaped");
    check(json_find_string(release_json, 0, "html_url", html, next), "html_url found");
    check(html == "https://github.com/mel0k1/Kiwi/releases/tag/v0.6.0", "html_url value");

    std::string missing;
    check(!json_find_string(release_json, 0, "no_such_key", missing, next), "missing key rejected");

    std::string json = release_json;
    size_t assets_at = json.find("\"assets\"");
    size_t cursor = assets_at;
    std::string best_name;
    std::string best_url;
    int best_score = 0;
    int assets_seen = 0;
    while (cursor != std::string::npos) {
        std::string name;
        std::string url;
        size_t after = 0;
        if (!json_next_asset(json, cursor, name, url, after)) break;
        ++assets_seen;
        int score = asset_score(name);
        if (score > best_score) {
            best_score = score;
            best_name = name;
            best_url = url;
        }
        cursor = after;
    }
    check(assets_seen == 2, "two assets parsed");
    check(best_name == "kiwi_0.6.0_amd64.deb", "best asset name");
    check(best_score == 4, "best asset score");
    check(best_url.find("download/v0.6.0/kiwi_0.6.0_amd64.deb") != std::string::npos,
        "best asset url");

    check(version_is_newer("v0.6.0", "0.5.0"), "0.6.0 newer than 0.5.0");
    check(version_is_newer("v0.5.1", "v0.5.0"), "0.5.1 newer than 0.5.0");
    check(version_is_newer("0.10.0", "v0.9.9"), "numeric 0.10 > 0.9");
    check(!version_is_newer("v0.5.0", "0.5.0"), "equal versions");
    check(!version_is_newer("v0.4.9", "0.5.0"), "older version");
    check(!version_is_newer("v0.5", "0.5.0"), "shorter equal version");
    check(version_is_newer("v0.5", "0.4.9"), "shorter newer version");
    check(!version_is_newer("", "0.5.0"), "garbage version");

    if (failures > 0) {
        std::fprintf(stderr, "%d/%d checks failed\n", failures, checks_total);
        return 1;
    }
    std::printf("%d updater checks passed\n", checks_total);
    return 0;
}
