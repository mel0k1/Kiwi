<p align="center"><img src="assets/logo.png" width="160" alt="Талисман Kiwi"></p>

# Kiwi

![platform](https://img.shields.io/badge/platform-Linux%20%7C%20Windows-2f74c0)
![license](https://img.shields.io/badge/license-LGPL--2.1-e0a03d)
![engine](https://img.shields.io/badge/engine-Mustela-3a7d44)
![status](https://img.shields.io/badge/status-first_flight-3a7d44)

Приватный веб-браузер на движке [Mustela](https://github.com/mel0k1/Mustela). Талисман — птичка киви: маленькая, любопытная и очень скрытная насчёт своей норки.

English version: [README.md](README.md)

## Что такое Kiwi

Kiwi — это браузер-продукт поверх движка Mustela. Разделение строгое:

- **Mustela** отвечает за движок — встраивание WebKit, политику приватности, настройки, профили, хранение истории, производительность.
- **Kiwi** отвечает за продукт — полноценный интерфейс, центр приватности, экраны истории и загрузок, упаковку — всё, чего касается пользователь.

Kiwi работает с Mustela только через его публичный API. Никаких типов WebKit выше границы движка — никогда.

## Статус

Kiwi 0.3.0. Kiwi собирается на Linux поверх движка [Mustela](https://github.com/mel0k1/Mustela): GTK-окно с вкладками (favicon, перетаскивание), адресная строка на входном конвейере движка (`resolve_entry` — поиск по ключевым словам, passthrough URL), переключатель приватности, центр приватности, менеджер закладок и окно настроек. Каждый web-view приходит из view-библиотеки движка (`include/mustela/view.hpp`, `libmustela_view_gtk`) — privacy-конвейер (cookies, ITP, бюджет фингерпринта, трекер-блоклист, GPC, пер-сайтовый dressing), брендированные страницы ошибок, загрузки, JS-диалоги, запросы разрешений и попапы живут ниже границы UI, а сам Kiwi линкуется с GTK без единого типа WebKit.

- **K1 первый полёт (0.1.0)** — оболочка с вкладками, адресная строка на движке, переключатель приватности, экраны истории и загрузок.
- **K2 центр приватности (0.2.0)** — редактор пер-сайтовых оверрайдов с живым индикатором, экраны сканирования и очистки хранимых данных, MD3 персиковая тема.
- **K3 библиотека и настройки (0.3.0)** — менеджер закладок (звезда Ctrl+D в адресной строке, менеджер Ctrl+Shift+O), редактор поисковиков (шорткаты `!keyword`, выбор дефолтного), менеджер профилей (изолированные корни данных, переключение после перезапуска).

Сборка:

```bash
git clone https://github.com/mel0k1/Mustela.git ../Mustela
cmake -S ../Mustela -B ../Mustela/build -DCMAKE_BUILD_TYPE=Release
cmake --build ../Mustela/build
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
./build/kiwi
```

## Экосистема

| Проект | Роль |
|--------|------|
| [Mustela](https://github.com/mel0k1/Mustela) | Движок на WebKit: приватность, профили, производительность |
| Kiwi | Интерфейс браузера поверх Mustela |

## Дорожная карта

См. [docs/ROADMAP.md](docs/ROADMAP.md): K0 гнездование, K1 первый полёт (оболочка на Mustela), K2 центр приватности, K3 библиотека и настройки, K4 упаковка, K5 айдентика.

## Лицензия

Kiwi распространяется по [GNU LGPL-2.1](LICENSE).
