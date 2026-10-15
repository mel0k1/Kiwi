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

- Kiwi consumes Mustela through its public API only: settings, privacy policy, history, profiles.
- No WebKit types above the engine boundary. If a feature needs a new engine capability, it lands in Mustela first.
- Kiwi is portable across Mustela ports: the same UI runs on the GTK port today and the WinCairo port later.
