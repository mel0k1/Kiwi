<p align="center"><img src="assets/logo.png" width="160" alt="Kiwi mascot"></p>

# Kiwi

![platform](https://img.shields.io/badge/platform-Linux%20%7C%20Windows-2f74c0)
![license](https://img.shields.io/badge/license-LGPL--2.1-e0a03d)
![engine](https://img.shields.io/badge/engine-Mustela-3a7d44)
![status](https://img.shields.io/badge/status-nesting-d95f5f)

Privacy-first web browser powered by the [Mustela](https://github.com/mel0k1/Mustela) engine. The mascot is a kiwi bird: small, curious, and very private about its burrow.

Русская версия: [README.ru.md](README.ru.md)

## What is Kiwi

Kiwi is the browser product built on top of the Mustela engine. The split is strict:

- **Mustela** owns the engine work — WebKit embedding, privacy policy, settings, profiles, history storage, performance.
- **Kiwi** owns the product — the full UI, privacy center, history and downloads screens, packaging, everything a user touches.

Kiwi consumes Mustela through its public API only. No WebKit types above the engine boundary, ever.

## Status

Kiwi 0.4.0. Kiwi builds on Linux against the [Mustela](https://github.com/mel0k1/Mustela) engine: a GTK window with tabs (favicons, drag-reorderable), an address bar driven by the engine's entry pipeline (`resolve_entry` — keyword search shortcuts, URL passthrough), a privacy toggle, a privacy center, a bookmarks manager and a settings window. Every web view comes from the engine's view library (`include/mustela/view.hpp`, `libmustela_view_gtk`) — the privacy pipeline (cookies, ITP, fingerprint budget, tracker blocklist, GPC, per-site dressing), branded error pages, downloads, JS dialogs, permission prompts and popups all live below the UI boundary, and Kiwi itself links GTK without a single WebKit type.

- **K1 first flight (0.1.0)** — tabbed shell, engine-driven address bar, privacy toggle, history and downloads screens.
- **K2 privacy center (0.2.0)** — per-site override editor with a live toolbar indicator, stored-data scan/clear screens, Material Design 3 peach theme.
- **K3 library and settings (0.3.0)** — bookmarks manager (Ctrl+D star in the address bar, Ctrl+Shift+O manager), search engines editor (`!keyword` shortcuts, default engine), profile manager (per-profile data roots, restart-safe switching).
- **View slice 2 UI (0.4.0)** — find-in-page bar over the engine's find controller (Ctrl+F, F3/Shift+F3, Esc closes, live hit/miss hint), an MD3 context menu (open link/image in a new tab, copy addresses, edit commands, "Search with …" over the default engine through the engine's selection bridge, inspect element), and a devtools toggle (Ctrl+Shift+I) docking the engine's inspector.

Build:

```bash
git clone https://github.com/mel0k1/Mustela.git ../Mustela
cmake -S ../Mustela -B ../Mustela/build -DCMAKE_BUILD_TYPE=Release
cmake --build ../Mustela/build
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
./build/kiwi
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
