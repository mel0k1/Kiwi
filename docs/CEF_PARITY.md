# Kiwi on CEF — parity matrix

Since 0.11.0 the Windows build of Kiwi runs on CEF (Chromium 152, pinned per
release) with **mustela-core** as a static library. The WinCairo view port of
the Mustela engine is retired: it was the source of every crash and workaround
shipped in 0.8.0–0.10.0. Linux keeps the full GTK + Mustela (`mustela_view_gtk`)
stack untouched.

The engine seam survived the swap: Kiwi-CEF links `mustela_core` (settings,
privacy policy, search pipeline, blocklist, fingerprint scripts, bookmarks,
profile layout) and adds a CEF layer on top. No WebKit types anywhere, and no
Mustela view library on Windows anymore.

## What maps where

| Feature | GTK + Mustela (Linux) | Kiwi-CEF (Windows) | Status |
|---|---|---|---|
| Settings (TOML, hot-shape) | mustela-core `load_settings` | mustela-core `load_settings` | done |
| Address bar pipeline | `resolve_entry` | `resolve_entry` | done |
| Start page (`kiwi://start`) | shared `start_page.cpp` over `file://` | same module, same file URI | done |
| Bookmark tiles on start page | engine `BookmarkStore` | engine `BookmarkStore` | done |
| Tracker blocklist | engine view layer (content extensions) | `OnBeforeResourceLoad` → `Blocklist::hit` | done |
| Third-party cookie policy | engine per-context policy | `CefCookieAccessFilter` (send + save) | done |
| Cookie policy `never` | engine | same filter, blanket deny | done |
| Fingerprint: WebGL / canvas | mustela-core scripts injected by engine | same scripts via `CefRenderProcessHandler::OnContextCreated` (before page scripts) | done |
| Fingerprint: remote fonts | engine resource filtering | `RT_FONT` requests cancelled in `OnBeforeResourceLoad` | done |
| Referrer trim | mustela-core script | same script | done |
| GPC | mustela-core script | script + `Sec-GPC: 1` header on every request | done |
| Private tabs | ephemeral WebKit context | dedicated incognito `CefRequestContext` (empty `cache_path`), shared across private tabs | done |
| Downloads | engine session callbacks | `CefDownloadHandler::OnBeforeDownload` → Downloads dir (or `downloads.dir`) | done |
| Media access (camera/mic/geo) | engine permission flow, deny defaults | `OnRequestMediaAccessPermission` → cancel all | done |
| Hyperlink auditing off | engine hardening | `PING` method cancelled | done |
| DoH | engine hook | Chromium `DnsOverHttps` feature params (secure mode, custom template) | experimental |
| Autoplay block | engine hardening | `autoplay-policy=user-gesture-required` | done |
| Tabs | GTK notebook, favicons, drag-reorder | custom-drawn chrome: caption-row tab chips (classic) or vertical rail (Zen) with favicons, loading spinner, private badge, per-tab close, strip context menu, middle-click close, drag-reorder with sliding chips, scrollable strip/rail (wheel forwarding + draggable thumb, active tab auto-reveal), tear-off: drag past the strip (or "Move tab to new window") re-parents the tab into a fresh shell window at the drop point, dock-back on release over the strip | good |
| Multi-window | multiple GTK windows | one `CefShell` window per browser window, shared message loop, last window closed quits; tear-off windows carry their browser alive (`SetParent` + client re-point), private context follows the browser; session.toml still mirrors the primary window only | good |
| Toolbar | cairo MD3 icons | GDI+ vector icon set (anti-aliased back/forward/reload/star/gear/plus/close/palette/shield/database/home), pill omnibox with focus ring, palettes from the ready-made theme set or the Windows app mode ("auto") | good |
| Window chrome | GTK headerbar | custom caption (drawn min/max/close, HTCAPTION drag, snap layouts), Win11 rounded corners + backdrop picker (off / Mica / acrylic frosted glass; the 0.16.0 "Mica" wrote DWMSBT_NONE and never showed), chrome themes: auto / light / dark / nord / dracula / catppuccin / solarized / sakura (pastel pink) / crimson (black-red) plus a custom #RRGGBB accent override, applied live (no restart) with live re-skin of every aux dialog; the zen rail's host-facing edge carries a drag grip — drag it sideways past the threshold and the panel flips side live, persists and re-syncs the settings dialog | done |
| Popups | engine-owned tabs | `OnBeforePopup` → new Kiwi tab, opener privacy inherited | done |
| History | engine store + history screen | mustela-core HistoryStore + Ctrl+H window (Open/Clear) | done |
| Session restore | engine store + UI | profile `session.toml` saved on close, restored on launch | done |
| Tab favicons | engine favicon store | `OnFaviconURLChange` → `CefURLRequest` download → ICO/PNG/JPEG/BMP decode → tab chip + `profile/favicons/<host>.png` disk cache shared with start tiles (private tabs never fetch) | done |
| Per-site rules (JS/cookies/fingerprint per site) | engine `resolve_site` + UI editor | shared `sites.toml` store + gear-button editor; enforcement: cookie deny in `CefCookieAccessFilter`, fingerprint allow/deny per frame in the render process, font blocking + zoom via `resolve_site` in `OnBeforeResourceLoad`, JS deny cancels external script resources (inline scripts still run — no per-origin toggle in Chromium) | done (JS partial) |
| Data-clear screens | engine `ClearRequest` API + UI | "Clear browsing data..." dialog: history (mustela-core store), cookies (global `CefCookieManager::DeleteCookies`), HTTP cache, HTTP-auth credentials and certificate exceptions via the CEF 152 `CefRequestContext` clear APIs (global context; private tabs keep their ephemeral context) | done |
| Find in page | engine find controller + UI bar | `CefFindHandler` + find bar (Ctrl+F, F3/Shift+F3, Esc, "n of m" counter per tab) | done |
| Context menu | engine `ContextMenuInfo` + MD3 menu | `CefContextMenuHandler` custom menu (navigation, link/image actions, clipboard, edit commands, search selection, view source, inspect) + tab strip menu | done |
| Devtools | engine inspector + Ctrl+Shift+I | docked: Ctrl+Shift+I opens a child DevTools browser docked to the bottom, left or right edge (Alloy `SetAsChild` + `DevToolsClient` lifespan hook) behind a draggable splitter; side picked via right-click on the splitter, persisted in `style.toml` (`devtools_dock`); split size persists, one instance, follows its tab | done |
| MHTML save | engine `save_mhtml` | not available (Chromium has no public MHTML API) | dropped |
| Web notifications | engine permission flow → `GNotification` | not implementable yet: CEF 152 exposes no web-notification API (`CefPermissionHandler` covers media access and permission prompts only) — revisit on the next CEF bump | open |
| Bookmarks manager UI | K3 screen | manager window over the shared store (Ctrl+Shift+O, star button) | done |
| Search engines editor | K3 screen | "Search engines..." window in settings: add/edit/remove engines (keyword, name, `%s` template) and the default over the shared mustela-core store; the omnibox picks changes up live through `resolve_entry` (`!keyword` included) | done |
| Settings UI | K2/K3 screens | Firefox-style sectioned settings window (Ctrl+,): navigation sidebar (General / Appearance / Search / Privacy / Data) with painted glyphs, one page per section, instant apply (edits commit on focus-out/close) with an "All changes saved" flash; Appearance holds shell style, panel side (left/right zen rail — also drag-switchable on the rail grip), backdrop picker, the theme chip grid (nine chips) and the custom accent picker (ChooseColor); General grew session-restore / history / start-in-private toggles; per-site rules editor + search engines editor + clear-browsing-data dialog + update check live under Data/Search | done |
| Chromium services trim | n/a | Translate, MediaRouter, OptimizationHints disabled; no component updates | done |
| Crash diagnostics | engine + `crash.log`/minidump | same `diagnostics.cpp` (shared), plus CEF `cef.log` | done |
| Chromium sandbox | n/a | enabled via the CEF bootstrap loader: client builds as `kiwi.dll` exporting `RunWinMain`, `bootstrap.exe` renamed to `kiwi.exe` prepares the sandbox context for `CefExecuteProcess`/`CefInitialize` (`cef_sandbox.lib` no longer exists in CEF 138+) | done |
| Update checker | libsoup on GTK | manual check in settings over CEF URL request + semver compare | done |

## Deliberately cut (кастрируем)

- WinCairo view port, mingw-w64 cross toolchain, WebKit runtime assembly
  scripts, pixbuf loader cache tricks — the entire 0.8.0–0.10.0 Windows
  workaround pile.
- GTK dependency on Windows (the old GTK chrome looked alien and shipped a
  second UI stack).
- MHTML export on Windows (no Chromium public API; stays on Linux).

## Pinning

- CEF: `152.0.14+g8a0b5b9+chromium-152.0.7977.167`, windows64 minimal, sha1
  `0bad9c7d50f2fbf508bdf47e8f885590bb4d19ae` (checked in CI before use).
- mustela-core: Mustela tag `v0.25.1` via CMake FetchContent.

The CEF pin is a security treadmill by design: Chromium CVEs land weekly and
CEF follows Chromium stable roughly monthly. The CI download is a two-line
bump (version + sha1); skipping the bump for months means shipping a stale,
vulnerable engine.
