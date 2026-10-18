# Architecture

Kiwi is the browser product. Since 0.11.0 the build is split per platform, and both paths share the same product logic:

- **Linux** — the GTK shell over the Mustela engine's view library. Kiwi never talks to WebKit directly; everything below the UI goes through `mustela::View`.
- **Windows** — a native Win32 shell on CEF (Chromium, pinned per release). The engine's WinCairo view port is retired; Kiwi-CEF links **mustela-core** (the engine's platform-agnostic core, fetched as a static library) and implements the privacy surface over CEF's request/render handler contracts.

## Layers (Linux — GTK + Mustela)

```
+-----------------------------+
| Kiwi UI (GTK3)              |   this repository
+-----------------------------+
| Mustela engine              |   github.com/mel0k1/Mustela
+-----------------------------+
| WebKit                      |   upstream
+-----------------------------+
```

## Rules

- Kiwi consumes Mustela through its public API only: settings, privacy policy, history, profiles, search engines and the view surface (`include/mustela/view.hpp` + `libmustela_view_gtk`).
- No WebKit types above the engine boundary. If a feature needs a new engine capability, it lands in Mustela first — the view library already carries the privacy pipeline, per-site dressing, error pages, downloads, dialogs, permissions and popups.
- Kiwi is portable across Mustela ports: the UI talks to `ViewSession`/`View` through a port-neutral header (`native_widget()` returns the platform widget), so a future WinCairo view library slots in without UI changes.

## Rules (both paths)

- No WebKit/Chromium types in product logic. On Linux the engine hides WebKit; on Windows the CEF layer (`src/cef/`) is the only place that includes `include/cef_*.h`.
- Shared product code stays platform-free: `start_page.cpp` (start page), `diagnostics.cpp` (crash logging) compile in both builds; `diagnostics_gtk.cpp` is GTK-only.
- mustela-core is the single source of truth for settings, privacy policy, search (`resolve_entry`), blocklist, fingerprint scripts and bookmarks — identical behavior on both platforms.

## Consumption (Linux)

```
+-----------------------------+
| Kiwi UI (GTK3)              |   src: window, tabs, address bar, screens
+-----------------------------+
| include/mustela/*.hpp       |   mustela.hpp (core) + view.hpp (views)
+-----------------------------+
| libmustela_view_gtk.a       |   engine view library (WebKitGTK sealed inside)
| libmustela_core.a           |   engine core (settings, policy, stores)
+-----------------------------+
| WebKitGTK                   |   linked by the engine, never by Kiwi
+-----------------------------+
```

The build takes `MUSTELA_ROOT` (engine checkout, default `../Mustela`) and links the two static libraries from `MUSTELA_BUILD_DIR`; Kiwi's own dependency is GTK3 via pkg-config.

## Consumption (Windows, KIWI_WITH_CEF)

```
+-----------------------------+
| Kiwi Win32 shell            |   src/cef/win: window, tab strip, omnibox, glyphs
+-----------------------------+
| Kiwi CEF layer              |   src/cef: app/render/request handlers, TabHost bridge
+-----------------------------+
| mustela_core.a              |   fetched from Mustela v0.25.1 (FetchContent)
+-----------------------------+
| CEF (Chromium, pinned)      |   binary distribution, sha1-checked in CI
+-----------------------------+
```

`cmake/kiwi-cef.cmake` drives the path: it fetches mustela-core, loads the CEF distribution (`CEF_ROOT`), builds `libcef_dll_wrapper` and produces `kiwi.dll` exporting `RunWinMain`; the distribution's `bootstrap.exe` is copied next to it as `kiwi.exe` and loads the DLL, preparing the Chromium sandbox context that flows into `CefExecuteProcess`/`CefInitialize` (the CEF 138+ bootstrap model; `cef_sandbox.lib` no longer exists). Settings live in `%LocalAppData%\kiwi\settings.toml` (same TOML shape as Linux); per-site rules in `%LocalAppData%\kiwi\sites.toml` (same store the GTK build edits); CEF cache under `%LocalAppData%\kiwi\cef`; diagnostics (crash.log, minidump, cef.log) under `%LocalAppData%\kiwi`. The feature mapping and honest gaps live in [CEF_PARITY.md](CEF_PARITY.md).
