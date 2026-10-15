<p align="center"><img src="assets/logo.png" width="160" alt="Талисман Kiwi"></p>

# Kiwi

![platform](https://img.shields.io/badge/platform-Linux%20%7C%20Windows-2f74c0)
![license](https://img.shields.io/badge/license-LGPL--2.1-e0a03d)
![engine](https://img.shields.io/badge/engine-Mustela-3a7d44)
![status](https://img.shields.io/badge/status-nesting-d95f5f)

Приватный веб-браузер на движке [Mustela](https://github.com/mel0k1/Mustela). Талисман — птичка киви: маленькая, любопытная и очень скрытная насчёт своей норки.

English version: [README.md](README.md)

## Что такое Kiwi

Kiwi — это браузер-продукт поверх движка Mustela. Разделение строгое:

- **Mustela** отвечает за движок — встраивание WebKit, политику приватности, настройки, профили, хранение истории, производительность.
- **Kiwi** отвечает за продукт — полноценный интерфейс, центр приватности, экраны истории и загрузок, упаковку — всё, чего касается пользователь.

Kiwi работает с Mustela только через его публичный API. Никаких типов WebKit выше границы движка — никогда.

## Статус

Гнездуемся. Движок строится в [Mustela](https://github.com/mel0k1/Mustela) (сейчас M1.5: политика приватности с переключением на лету, хранилище истории); интерфейс Kiwi стартует, когда API движка устаканится.

## Экосистема

| Проект | Роль |
|--------|------|
| [Mustela](https://github.com/mel0k1/Mustela) | Движок на WebKit: приватность, профили, производительность |
| Kiwi | Интерфейс браузера поверх Mustela |

## Дорожная карта

См. [docs/ROADMAP.md](docs/ROADMAP.md): K0 гнездование, K1 первый полёт (оболочка на Mustela), K2 центр приватности, K3 упаковка, K4 айдентика.

## Лицензия

Kiwi распространяется по [GNU LGPL-2.1](LICENSE).
