<p align="center"><img src="assets/logo.png" width="160" alt="Kiwi mascot"></p>

# Kiwi

![platform](https://img.shields.io/badge/platform-Linux%20%7C%20Windows-2f74c0)
![license](https://img.shields.io/badge/license-LGPL--2.1-e0a03d)
![engine](https://img.shields.io/badge/engine-Mustela-3a7d44)
![status](https://img.shields.io/badge/status-0.6.0_identity_platform-d95f5f)

Privacy-first web browser powered by the [Mustela](https://github.com/mel0k1/Mustela) engine. The mascot is a kiwi bird: small, curious, and very private about its burrow.

Русская версия: [README.ru.md](README.ru.md)

## What is Kiwi

Kiwi is the browser product built on top of the Mustela engine. The split is strict:

- **Mustela** owns the engine work — WebKit embedding, privacy policy, settings, profiles, history storage, performance.
- **Kiwi** owns the product — the full UI, privacy center, history and downloads screens, packaging, everything a user touches.

Kiwi consumes Mustela through its public API only. No WebKit types above the engine boundary, ever.

## Status

Kiwi 0.8.0. Kiwi builds on Linux and Windows against the [Mustela](https://github.com/mel0k1/Mustela) engine: a GTK window with tabs (favicons, drag-reorderable), an address bar driven by the engine's entry pipeline (`resolve_entry` — keyword search shortcuts, URL passthrough), a privacy toggle, a privacy center, a bookmarks manager and a settings window. Every web view comes from the engine's view library (`include/mustela/view.hpp`, `libmustela_view_gtk` on Linux, `libmustela_view_win` over WinCairo on Windows) — the privacy pipeline (cookies, ITP, fingerprint budget, tracker blocklist, GPC, per-site dressing), branded error pages, downloads, JS dialogs, permission prompts and popups all live below the UI boundary, and Kiwi itself links GTK without a single WebKit type. The project website lives at [mel0k1.github.io/Kiwi](https://mel0k1.github.io/Kiwi/).

- **K1 first flight (0.1.0)** — tabbed shell, engine-driven address bar, privacy toggle, history and downloads screens.
- **K2 privacy center (0.2.0)** — per-site override editor with a live toolbar indicator, stored-data scan/clear screens, Material Design 3 peach theme.
- **K3 library and settings (0.3.0)** — bookmarks manager (Ctrl+D star in the address bar, Ctrl+Shift+O manager), search engines editor (`!keyword` shortcuts, default engine), profile manager (per-profile data roots, restart-safe switching).
- **View slice 2 UI (0.4.0)** — find-in-page bar over the engine's find controller (Ctrl+F, F3/Shift+F3, Esc closes, live hit/miss hint), an MD3 context menu (open link/image in a new tab, copy addresses, edit commands, "Search with …" over the default engine through the engine's selection bridge, inspect element), and a devtools toggle (Ctrl+Shift+I) docking the engine's inspector.
- **Packaging and updates (0.5.0)** — a "Check for updates" button in Settings → About & updates: a strictly manual check against GitHub Releases (no background traffic), a platform asset picker, semver comparison, release notes and a download into the Downloads directory; a deb with install rules (icon, `.desktop`, AppStream metainfo, shlib-deps), a Flatpak manifest on the GNOME 48 runtime, and a tag-triggered release job that attaches both to the GitHub Release the updater reads. Windows stays an honest gap: MSYS2 removed the WebKitGTK packages, so there is no maintained Windows runtime to package against yet.
- **Identity and platform dressing (0.6.0)** — the K5 milestone: a full freedesktop hicolor icon set generated from the mascot artwork (`scripts/make_icons.py`, 16–256 px), a Material-Design-3 pastel-peach project website deployed to GitHub Pages (`site/`, deployed by the `site` workflow), the AppStream homepage pointing at it, and the engine view slice 3 surfaced in the UI: page saving as MHTML (Ctrl+S, native save dialog, suggested name from the page title, status-bar confirmation), HTML5 element fullscreen requests fullscreen the Kiwi window (F11 toggles it manually), and web notifications render through `GNotification` on a registered `GApplication` (dropped quietly when no session bus exists).
- **Windows port (0.8.0)** — Kiwi builds and runs on Windows for the first time: the chrome stays GTK3 (MSYS2 MinGW-w64), every web view comes from the engine's Windows view library (`mustela_view_win` over the WinCairo WebKit C API), and a CI job builds engine + browser, runs the updater tests, assembles a portable zip (WebKit runtime, GTK runtime, pixbuf loaders, launcher) and smoke-launches it. The native `HWND` view is hosted inside the GTK window by a small view-host layer (`src/viewhost*.cpp`) with geometry sync plus a keyboard hook that keeps the shortcut set alive while a web view holds focus. Honest limits of this first pass: devtools, page saving, notifications and the edit-command bridge degrade to the engine's documented Windows no-ops, the hosted view is not DPI-scaled, and the zip is portable-experimental until a manual pass on real hardware.

Build:

```bash
git clone https://github.com/mel0k1/Mustela.git ../Mustela
cmake -S ../Mustela -B ../Mustela/build -DCMAKE_BUILD_TYPE=Release
cmake --build ../Mustela/build
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
./build/kiwi
```

Windows (MSYS2 `MINGW64` shell; the CI `windows` job runs exactly this pipeline and packs a portable zip):

```bash
pacman -S mingw-w64-x86_64-toolchain mingw-w64-x86_64-cmake mingw-w64-x86_64-ninja \
          mingw-w64-x86_64-gtk3 mingw-w64-x86_64-libsoup3
git clone https://github.com/mel0k1/Mustela.git ../Mustela
pwsh ../Mustela/scripts/fetch-win-headers.ps1 -Destination build/sdk/include
pwsh ../Mustela/scripts/fetch-win-runtime.ps1 -Destination build/runtime
mkdir -p build/sdk/lib && cp build/runtime/runtime/bin64/*.dll build/sdk/lib/
cmake -S ../Mustela -B ../Mustela/build -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DMUSTELA_BUILD_SHELL=OFF -DMUSTELA_BUILD_TESTS=OFF -DMUSTELA_BUILD_BENCH=OFF \
  -DMUSTELA_BUILD_VIEW=ON -DMUSTELA_WEBKIT_SDK="$(pwd -W)/build/sdk"
cmake --build ../Mustela/build
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DMUSTELA_WEBKIT_SDK="$(pwd -W)/build/sdk"
cmake --build build
./build/kiwi.exe
```

## Ecosystem

| Project | Role |
|---------|------|
| [Mustela](https://github.com/mel0k1/Mustela) | WebKit-based engine: privacy, profiles, performance |
| Kiwi | Browser UI on top of Mustela |

## Roadmap

See [docs/ROADMAP.md](docs/ROADMAP.md): K0 nesting, K1 first flight (UI shell on Mustela), K2 privacy center, K3 library and settings, K4 packaging, K5 identity.

## License

Kiwi is licensed under the [GNU LGPL-2.1](LICENSE).
