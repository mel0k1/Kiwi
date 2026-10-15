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

Nesting. The engine is being built in [Mustela](https://github.com/mel0k1/Mustela) (currently M1.5: privacy policy with runtime toggle, history store); the Kiwi UI starts once the engine API settles.

## Ecosystem

| Project | Role |
|---------|------|
| [Mustela](https://github.com/mel0k1/Mustela) | WebKit-based engine: privacy, profiles, performance |
| Kiwi | Browser UI on top of Mustela |

## Roadmap

See [docs/ROADMAP.md](docs/ROADMAP.md): K0 nesting, K1 first flight (UI shell on Mustela), K2 privacy center, K3 packaging, K4 identity.

## License

Kiwi is licensed under the [GNU LGPL-2.1](LICENSE).
