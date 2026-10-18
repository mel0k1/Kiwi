<p align="center"><img src="assets/logo.png" width="160" alt="Талисман Kiwi"></p>

# Kiwi

![platform](https://img.shields.io/badge/platform-Linux%20%7C%20Windows-2f74c0)
![license](https://img.shields.io/badge/license-LGPL--2.1-e0a03d)
![engine](https://img.shields.io/badge/engine-Mustela-3a7d44)
![status](https://img.shields.io/badge/status-0.5.0_packaging-3a7d44)

Приватный веб-браузер на движке [Mustela](https://github.com/mel0k1/Mustela). Талисман — птичка киви: маленькая, любопытная и очень скрытная насчёт своей норки.

English version: [README.md](README.md)

## Что такое Kiwi

Kiwi — это браузер-продукт поверх движка Mustela. Разделение строгое:

- **Mustela** отвечает за движок — встраивание WebKit, политику приватности, настройки, профили, хранение истории, производительность.
- **Kiwi** отвечает за продукт — полноценный интерфейс, центр приватности, экраны истории и загрузок, упаковку — всё, чего касается пользователь.

Kiwi работает с Mustela только через его публичный API. Никаких типов WebKit выше границы движка — никогда.

## Статус

Kiwi 0.8.0. Kiwi собирается на Linux и Windows поверх движка [Mustela](https://github.com/mel0k1/Mustela): GTK-окно с вкладками (favicon, перетаскивание), адресная строка на входном конвейере движка (`resolve_entry` — поиск по ключевым словам, passthrough URL), переключатель приватности, центр приватности, менеджер закладок и окно настроек. Каждый web-view приходит из view-библиотеки движка (`include/mustela/view.hpp`, `libmustela_view_gtk` на Linux, `libmustela_view_win` поверх WinCairo на Windows) — privacy-конвейер (cookies, ITP, бюджет фингерпринта, трекер-блоклист, GPC, пер-сайтовый dressing), брендированные страницы ошибок, загрузки, JS-диалоги, запросы разрешений и попапы живут ниже границы UI, а сам Kiwi линкуется с GTK без единого типа WebKit. Сайт проекта — [mel0k1.github.io/Kiwi](https://mel0k1.github.io/Kiwi/).

- **K1 первый полёт (0.1.0)** — оболочка с вкладками, адресная строка на движке, переключатель приватности, экраны истории и загрузок.
- **K2 центр приватности (0.2.0)** — редактор пер-сайтовых оверрайдов с живым индикатором, экраны сканирования и очистки хранимых данных, MD3 персиковая тема.
- **K3 библиотека и настройки (0.3.0)** — менеджер закладок (звезда Ctrl+D в адресной строке, менеджер Ctrl+Shift+O), редактор поисковиков (шорткаты `!keyword`, выбор дефолтного), менеджер профилей (изолированные корни данных, переключение после перезапуска).
- **UI поверх view slice 2 (0.4.0)** — панель поиска по странице поверх find-контроллера движка (Ctrl+F, F3/Shift+F3, Esc закрывает, живая подсказка хит/промах), MD3 контекстное меню (открыть ссылку/картинку в новой вкладке, копировать адреса, команды редактирования, «Search with …» через дефолтный поисковик и мост выделения движка, инспекция элемента) и переключатель devtools (Ctrl+Shift+I) с докингом инспектора движка.
- **Пакетирование и обновления (0.5.0)** — кнопка «Проверить на обновления» в Настройках → About & updates: строго ручная проверка GitHub Releases (никакого фонового трафика), выбор пакета под платформу, semver-сравнение, примечания релиза и скачивание в каталог Downloads; deb с install-правилами (иконка, `.desktop`, AppStream metainfo, shlib-deps), Flatpak-манифест на рантайме GNOME 48 и релизный job по тегу, который прикладывает оба артефакта к GitHub Release — тому самому, который читает апдейтер. Windows — честный пробел: MSYS2 удалил пакеты WebKitGTK, поэтому поддерживаемого рантайма для сборки под Windows пока нет.
- **Идентичность и платформенный dressing (0.6.0)** — веха K5: полный набор hicolor-иконок из арта маскота (`scripts/make_icons.py`, 16–256 px), сайт проекта в стиле MD3 персиковой палитры, разворачиваемый на GitHub Pages (`site/`, деплой workflow'ом `site`), AppStream-homepage указывает на него, а view slice 3 движка выведен в UI: сохранение страницы в MHTML (Ctrl+S, нативный диалог, предлагаемое имя из заголовка страницы, подтверждение в статус-баре), запросы HTML5 element-fullscreen переводят окно Kiwi в fullscreen (F11 — ручной тумблер), веб-уведомления рендерятся через `GNotification` на зарегистрированном `GApplication` (тихо отбрасываются без session bus).
- **Windows-порт (0.8.0)** — Kiwi впервые собирается и запускается на Windows: хром остаётся GTK3 (MSYS2 MinGW-w64), web-view'ы приходят из Windows view-библиотеки движка (`mustela_view_win` поверх WinCairo WebKit C API), а CI-джоба собирает движок и браузер, прогоняет тесты апдейтера, упаковывает портативный zip (WebKit-рантайм, GTK-рантайм, pixbuf-лоадеры, лаунчер) и делает smoke-запуск. Нативный `HWND`-вьюха хостится внутри GTK-окна через небольшой view-host слой (`src/viewhost*.cpp`) с синхронизацией геометрии и клавиатурным хуком, который держит горячие клавиши живыми, пока фокус у web-view. Честные лимиты первого прохода: devtools, сохранение страницы, уведомления и мост команд редактирования деградируют до задокументированных Windows no-op движка, хостед-вьюха не масштабируется под DPI, а zip остаётся portable-experimental до ручного прогона на реальном железе.

Сборка:

```bash
git clone https://github.com/mel0k1/Mustela.git ../Mustela
cmake -S ../Mustela -B ../Mustela/build -DCMAKE_BUILD_TYPE=Release
cmake --build ../Mustela/build
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
./build/kiwi
```

Windows (оболочка MSYS2 `MINGW64`; CI-джоба `windows` гоняет ровно этот пайплайн и пакует портативный zip):

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

## Экосистема

| Проект | Роль |
|--------|------|
| [Mustela](https://github.com/mel0k1/Mustela) | Движок на WebKit: приватность, профили, производительность |
| Kiwi | Интерфейс браузера поверх Mustela |

## Дорожная карта

См. [docs/ROADMAP.md](docs/ROADMAP.md): K0 гнездование, K1 первый полёт (оболочка на Mustela), K2 центр приватности, K3 библиотека и настройки, K4 упаковка, K5 айдентика.

## Лицензия

Kiwi распространяется по [GNU LGPL-2.1](LICENSE).
