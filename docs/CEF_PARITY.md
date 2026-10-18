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
| Tabs | GTK notebook, favicons, drag-reorder | Win32 owner-drawn tab control, per-tab close buttons, private prefix | good |
| Toolbar | cairo MD3 icons | GDI-drawn glyphs (back/forward/reload/new/close) | basic |
| Popups | engine-owned tabs | `OnBeforePopup` → new Kiwi tab, opener privacy inherited | done |
| History | engine store + history screen | not recorded yet | open |
| Session restore | engine store + UI | not implemented | open |
| Per-site rules (JS/cookies/fingerprint per site) | engine `resolve_site` + UI editor | shared `sites.toml` store + gear-button editor; enforcement: cookie deny in `CefCookieAccessFilter`, fingerprint allow/deny per frame in the render process, font blocking + zoom via `resolve_site` in `OnBeforeResourceLoad`, JS deny cancels external script resources (inline scripts still run — no per-origin toggle in Chromium) | done (JS partial) |
| Data-clear screens | engine `ClearRequest` API + UI | no UI; Chromium keeps its own cache under `config/cef` | open |
| Find in page | engine find controller + UI bar | `CefFindHandler` + find bar (Ctrl+F, F3/Shift+F3, Esc, "n of m" counter per tab) | done |
| Context menu | engine `ContextMenuInfo` + MD3 menu | Chromium default menu | open |
| Devtools | engine inspector + Ctrl+Shift+I | `ShowDevTools`/`CloseDevTools` toggle (Ctrl+Shift+I), CEF-owned popup window | done |
| MHTML save | engine `save_mhtml` | not available (Chromium has no public MHTML API) | dropped |
| Web notifications | engine permission flow → `GNotification` | not implemented | open |
| Bookmarks manager UI | K3 screen | store is shared (Ctrl+D works, tiles show), no manager window | partial |
| Search engines editor | K3 screen | no UI; `settings.toml` is read by mustela-core | open |
| Settings UI | K2/K3 screens | no UI; edit `settings.toml` in `%LocalAppData%\kiwi` | open |
| Chromium services trim | n/a | Translate, MediaRouter, OptimizationHints disabled; no component updates | done |
| Crash diagnostics | engine + `crash.log`/minidump | same `diagnostics.cpp` (shared), plus CEF `cef.log` | done |
| Chromium sandbox | n/a | enabled via the CEF bootstrap loader: client builds as `kiwi.dll` exporting `RunWinMain`, `bootstrap.exe` renamed to `kiwi.exe` prepares the sandbox context for `CefExecuteProcess`/`CefInitialize` (`cef_sandbox.lib` no longer exists in CEF 138+) | done |
| Update checker | libsoup on GTK | not implemented on CEF yet | open |

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
