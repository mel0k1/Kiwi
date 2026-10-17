# Architecture

Kiwi is the browser product; it never talks to WebKit directly. Everything below the UI goes through the Mustela engine.

## Layers

```
+-----------------------------+
| Kiwi UI                     |   this repository
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

## Consumption

```
+-----------------------------+
| Kiwi UI (GTK3)              |   src/ui: window, tabs, address bar, screens
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
