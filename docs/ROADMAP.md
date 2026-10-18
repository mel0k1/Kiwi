# Roadmap

Milestones are capability-based; dates are intentionally not assigned.

## K0 — Nesting (current)

- Repository, mascot, license, documentation.
- Placeholder binary so the build system lives from day one.

## K1 — First flight (done)

- UI shell consuming the Mustela engine API: a GTK window with notebook tabs (favicons from the engine's favicon persistence, drag-reorderable, per-tab close), back/forward/reload/new-tab toolbar, an address bar driven by the engine entry pipeline (`resolve_entry` — `!keyword` search shortcuts, URL passthrough, host-like text) and a status bar with link hover, privacy markers and download progress. (done)
- Privacy toggle in the UI, wired to the engine policy: the toggle switches new tabs to the engine's private mode (ephemeral context, no history, no favicon persistence), private tabs are marked in the UI. (done)
- History and downloads screens: the history window lists the engine store (Ctrl+H, row-activate opens a tab, clear button) and the downloads window tracks live download progress (Ctrl+J) fed by the engine's session download callbacks; downloads honor the engine settings (`downloads.mode` auto or ask with a native save dialog). (done)
- Everything below the UI is the engine: session/view objects from `include/mustela/view.hpp`, the privacy pipeline, per-site dressing, branded error pages, JS dialogs, permission/certificate/auth prompts, popups and crash recovery come from `libmustela_view_gtk`; Kiwi links GTK3 and the two Mustela static libraries and contains zero WebKit types. (done)
- Honest limits (K1 slice): no session restore, bookmarks UI, find bar, site-rules editor or hot-reload yet — the engine surfaces exist; the UI screens landed with K2 and K3, the find bar with the engine view slice 2 below. Packaging waits for K4. (limits)

## K2 — Privacy center

- Per-site privacy overrides with visual indicators. (done, 0.2.0)
- Storage and cookie management screens. (done, 0.2.0)
- Import from other browsers. (done, 0.7.0 — engine-side bookmark import plus the settings-screen import handler)

## K3 — Library and settings (done)

- Bookmarks manager: a star in the address bar (Ctrl+D) toggles the current page; the manager window (Ctrl+Shift+O) lists, adds, edits, removes and opens bookmarks. Persistence goes through the engine's `BookmarkStore`, per profile. (done, 0.3.0)
- Search engines editor: add, edit and remove engines (keyword, display name, `%s` URL template), set the default engine; `!keyword` address-bar shortcuts resolve through the engine's `resolve_entry` pipeline. (done, 0.3.0)
- Profile manager: add, rename and remove profiles backed by the engine's `ProfileRegistry`, switch the active profile (applies on restart); per-profile data roots isolate history, bookmarks, autofill and stored data. (done, 0.3.0)

## Engine view slice 2 — find, devtools, context menu (done, 0.4.0)

- Find-in-page bar over the engine's find surface (`View::find`/`find_next`/`find_previous`/`find_finish`): Ctrl+F opens an MD3 bar between the toolbar and the tabs, typing searches live, F3/Shift+F3 or Enter steps through matches, Esc closes and clears; the engine's `find_result` callback drives a hit/miss hint. (done)
- MD3 context menu built from the engine's `ContextMenuInfo` hit-test flags: navigation, open link/image in a new tab, copy link/image address, copy/cut/paste/select-all through `View::execute_edit_command`, "Search with …" fetching the page selection through the engine's `selected_text` bridge and the default engine, "Inspect element". Returning true from the engine callback suppresses the stock menu. (done)
- Devtools toggle (Ctrl+Shift+I) over `View::show_devtools`/`hide_devtools`/`devtools_attached`, with the dock-state callback flashing the status bar. (done)

## K4 — Packaging (done, 0.5.0)

- Check for updates: an "About & updates" page in Settings with a manual "Check for updates" button — an on-demand query against the GitHub Releases API (libsoup, worker thread, progress in the same row), a platform asset picker (amd64 deb on Linux, exe/zip on Windows), semver comparison against the baked `KIWI_VERSION`, release notes preview, and a download into the user's Downloads directory with a "Show in folder" action. No background traffic, ever — the check happens only when the user presses the button. (done)
- Linux deb: install rules for the binary, the hicolor icon, the `.desktop` file and the AppStream metainfo; CPack DEB with shlib-deps-generated runtime dependencies (GTK, WebKitGTK, libsoup); the engine's web-extension module ships inside the package when built from a Mustela checkout. (done)
- Flatpak: `packaging/flatpak/io.github.mel0k1.Kiwi.yml` builds the engine (v0.23.0, view library on, web extension into `/app/lib/mustela/webext`) and Kiwi against the installed engine export targets on the GNOME 48 runtime; finish-args cover network, GPU, sockets, downloads and notifications. (done)
- Windows: the honest-limit story changed in 0.8.0 — there is still no WebKitGTK runtime on Windows, but Kiwi now builds against the engine's WinCairo-based view library (`mustela_view_win`) instead: the chrome stays GTK3 under MSYS2 MinGW-w64 and the web content comes from `libmustela_view_win`. A CI `windows` job builds engine + browser, runs the updater tests, packs a portable zip (WebKit runtime, GTK runtime, pixbuf loaders, launcher) and smoke-launches it. A signed installer and the release-asset hookup for the updater stay open. (portable zip done, 0.8.0; installer open)
- Release channel: a tag-triggered job attaches the deb and the Flatpak bundle to the GitHub Release, which is exactly what the update checker reads. (done)

## Engine view slice 3 — fullscreen, notifications, MHTML (done, 0.6.0)

- The engine view API moved 3 → 4 (Mustela v0.23.0): `ViewCallbacks::enter_fullscreen`/`leave_fullscreen`, `ViewCallbacks::notification(title, body)`, `View::save_mhtml(path, done)` and `suggested_save_name(title, uri)`. Kiwi consumes all of it: Ctrl+S opens a native save dialog (default folder from `downloads.dir` or the user Downloads directory, default name from `suggested_save_name`, MHTML filter, overwrite confirmation) and the status bar confirms the write; element fullscreen requests fullscreen the Kiwi window and leaving fullscreen restores it (F11 remains the manual toggle); notifications render through `GNotification` on a `GApplication` registered at startup (`io.github.mel0k1.Kiwi`, non-unique, held; without a session bus the registration fails quietly and notifications drop — same fail-safe shape as the engine harness).
- Honest limits: MHTML is a snapshot of the current DOM without scripts and is save-only (WebKitGTK has no public MHTML load path); notification display still requires the engine permission flow (`permissions.notifications` + per-site rule) before the callback fires; on Windows all three degrade to the engine's documented WinCairo pin no-ops. (limits)

## Windows port (done, 0.8.0)

- View hosting: the Windows view library hands out an `HWND` (`View::native_widget()`), so a small view-host layer (`src/viewhost*.cpp`) replaces the direct `GtkWidget*` casts — on Linux it stays a passthrough, on Windows each tab gets a `GtkDrawingArea` page whose map/unmap/size-allocate signals reparent (`SetParent`), position (`MoveWindow` from `gdk_window_get_origin` + `ScreenToClient`) and show/hide the native view. (done)
- Shortcuts: a `WH_KEYBOARD` thread hook plus a subclassed window proc translate the key combinations into the shared `kiwi_shortcut` dispatcher while a web view holds keyboard focus (GTK accelerators do not see keys landing in a foreign child HWND); combos outside the table pass through untouched, dialogs are excluded via `GetForegroundWindow`. (done)
- Build and bundle: MinGW-w64 (MSYS2) compiles engine core + `mustela_view_win` + Kiwi with zero MSVC-specific sources; import resolution goes straight through the runtime DLLs; the CI job assembles the portable zip (WebKit runtime + GTK/runtime DLLs via `ntldd` + gio modules + pixbuf loaders with a rewritten cache + glib schemas + a `kiwi.cmd` launcher) and smoke-launches the bundle. (done)
- Honest limits: devtools, page saving (MHTML), notifications, HTML5 fullscreen and the edit-command bridge are engine-side no-ops on the WinCairo pin; the hosted view is not DPI-scaled yet; popups land as notebook tabs (same as Linux) but never surface on top of the web view; installer and updater asset hookup remain open. (limits)

## K5 — Identity (done, 0.6.0)

- Final mascot artwork and application icon. The flat kiwi-bird artwork is the master (`assets/logo.png`, 256 px); `scripts/make_icons.py` generates the full freedesktop hicolor set (16/22/24/32/48/64/128/256) with LANCZOS downscales, and CMake installs every size — proper icons in taskbars, alt-tab switchers and shell dashboards instead of a single upscaled 256 px. 512 px stays out on purpose: upscaling flat artwork only blurs it. (done)
- Website and release channel. `site/index.html` is the project landing page in the MD3 pastel-peach palette (solid fills, 1px outlines, no gradients or shadows): hero with the mascot, capability cards, install instructions for the deb and the Flatpak bundle and links to the Releases page the in-browser updater reads; deployed to GitHub Pages at `mel0k1.github.io/Kiwi` by the tag- and push-triggered `site` workflow, and the AppStream homepage now points there. The release channel itself stays the tag-triggered job attaching the deb and the Flatpak bundle to the GitHub Release — which is exactly what the check-for-updates button reads, so the website, the updater and the release job all speak about the same artifacts. (done)

## Windows usability pass (done, 0.10.0)

First real-Windows feedback said: white page, "search is broken", crashes on every second action, no tabs, no start page, icons from the past. Each complaint traced to a concrete defect:

- White window with a dead address bar: the bundle copied only `*.dll` from the WebKit runtime, but the content and network layers are helper executables (`WebProcess.exe`, `NetworkProcess.exe`). The bundle script now copies every non-MiniBrowser `*.exe` and hard-fails when no WebProcess lands in the bundle. (done)
- Crashes: the view-host kept page records in a plain `std::vector<ViewPage>` — opening a second tab reallocated the vector and every earlier tab's map/unmap/size-allocate handlers dereferenced freed memory. Records now live in `std::vector<std::unique_ptr<ViewPage>>` (stable addresses). Closing a tab destroyed the engine `View` (and its native WebKit page) synchronously, sometimes inside a WebKit callback (`window.close()`); destruction now parks on an idle source. The engine side (Mustela v0.25.1) always calls the JS prompt listener (previously a missing embedder callback blocked the page forever) and validates popup creation before handing the wrapper to the embedder. (done)
- Tabs: the notebook hid the tab strip with a single tab; the strip is now always visible. (done)
- Start page: `kiwi://start` renders a self-contained MD3 page into the profile (`start.html`, loaded over `file://`) — search card wired to the default engine with client-side URL detection, bookmark tiles with favicons, clock, private-mode banner; the old engine-default homepage (`https://example.org`) migrates to it automatically, explicit homepages are kept. (done)
- Icons: GTK on Windows ships without any icon theme, so symbolic icon names rendered as broken fallbacks. All toolbar, find-bar, tab-close and bookmark-star glyphs are now cairo-drawn MD3 pixbufs with an ink/muted tint swap for disabled controls (`src/icons.cpp`). (done)
- Diagnostics: `%LocalAppData%\kiwi\crash.log` + `crash.dmp` via `SetUnhandledExceptionFilter` + `MiniDumpWriteDump`, `kiwi.log` for GTK criticals (same on Linux for the log). CI replaced the passive 20-second launch with a scripted `--self-test` (open/switch/close tabs, start page, omnibox, resize) on real Windows and Linux, failing on crash logs. (done)
- Honest limits: notification toasts on Windows still require the engine's embedder presentation layer; the self-test cannot click into web content (no UI automation), so in-page interaction stays manual-test territory for now. (limits)

## CEF transition (current, 0.11.0)

The decision behind this milestone: every Windows defect of 0.8.0–0.10.0 traced back to one root — the WebKit WinCairo port (experimental upstream, mingw cross-compilation, no real sandbox, no codecs, crashes on the smallest step sideways). Fixing the symptoms produced a growing pile of workarounds. The engine seam designed in K1 (no WebKit types above `mustela::View`) made the actual surgery small:

- Windows moves to CEF (Chromium, pinned per release): a native Win32 shell replaces the GTK chrome, and the engine's WinCairo view port plus the whole mingw pipeline are retired. The `windows` CI job is a two-artifact pipeline (portable zip + Inno Setup installer) over the CEF bundle. (done)
- mustela-core survives as a static library fetched from the Mustela repo (`v0.25.1`): settings pipeline, privacy policy, `resolve_entry`, blocklist, fingerprint scripts, bookmarks, profile layout. It is load-bearing in the CEF build, not dead weight. (done)
- Privacy surface on CEF: tracker blocklist and remote-font/ping blocking in `OnBeforeResourceLoad`, third-party/never cookie filtering via `CefCookieAccessFilter`, WebGL/canvas/referrer/GPC scripts injected in the render process before page scripts, media access denied, downloads into the user's Downloads directory, DoH via Chromium feature params. (done)
- Shell: Win32 tab strip (titles, private prefix, per-tab close via Ctrl+W and the toolbar button), GDI-drawn toolbar glyphs, omnibox over `resolve_entry`, start page shared with the GTK build, Ctrl+D bookmarking, hotkeys alive in web-content focus through `CefKeyboardHandler`. (done)
- Honest limits: no history/session restore on CEF yet, no per-site rules UI, no find bar, context menu is Chromium default, devtools and notifications not wired, update checker not ported, Chromium sandbox disabled until the `cef_sandbox.lib` linkage pass. Full matrix: `docs/CEF_PARITY.md`. (limits)

## CEF series

- C2 — polish (done, 0.12.0): per-tab close buttons in the strip (owner-drawn tabs with selection highlight, hover feedback and a hover-highlighted × that closes without switching selection), find bar over `CefFindHandler` (Ctrl+F, live search, Enter/F3 and Shift+F3 step, Esc closes, per-tab "n of m" counter), devtools toggle (Ctrl+Shift+I) over `ShowDevTools`/`CloseDevTools` in a CEF-owned popup window kept out of the tab strip by a dedicated client, and closing the last tab now closes the browser instead of leaving an empty shell. Honest limits: tab favicons and render-process crash recovery are not in this slice — favicons need a `OnFaviconURLChange` + download pass, crash recovery lands with the session work (C5); devtools opens detached (no docked view yet, matching the Win32 shell's window model).
- C3 — privacy parity (done, 0.13.0): per-site rules over `resolve_site` — the shared `sites.toml` store now drives the CEF layers (cookie deny in `CefCookieAccessFilter` with the filter attaching even under a global always-policy when a rule needs it, fingerprint allow/deny resolved per frame in the render process via the store path on the command line, per-site font blocking and zoom from the same decision, JS deny cancelling external script resources) plus a gear-button rules editor (list, host field, three default/allow/deny combos, preserves the zoom/user-agent/CSS fields it does not edit yet); header-level GPC stamps `Sec-GPC: 1` on every request; the Chromium sandbox is back through the CEF 138+ bootstrap model — the client builds as `kiwi.dll` exporting `RunWinMain`, loaded by the distribution's `bootstrap.exe` renamed to `kiwi.exe`, which prepares the sandbox context passed into `CefExecuteProcess`/`CefInitialize` (the old `cef_sandbox.lib` static-link recipe no longer exists). Honest limits: JS deny is external-scripts-only (inline scripts still run — Chromium has no per-origin script toggle; response-body filtering was rejected as brittle), per-site permissions stay deny-all (granting camera/mic silently is not an option), user-agent and CSS rule fields are preserved but not enforced yet, stored-data clear screens moved to a later slice.
- C4 — library and settings: bookmarks manager, search engines editor and settings screens over the shared mustela-core state; plus the shell style switch (classic Kiwi / Zen-flavoured layout) selected in settings.
- 0.13.1 — startup diagnostics (done): the "opens and immediately closes" report pass — every startup stage is traced to `%LocalAppData%\kiwi\startup.log`, failed `CefInitialize`/profile reads show an error dialog instead of a silent exit, `--self-test` returned and runs the real bundle in the Windows CI job (a written `crash.log` fails the job), the installer enforces the Windows 10 1607+ requirement of Chromium 152 and wipes the install directory before deploying so stale 0.10.x GTK files cannot form a mixed unlaunchable install, and the bundle no longer ships test/linker litter.
- C5 — session/history on CEF: history recording behind the privacy policy, session restore, update checker port.
- C6 — Linux evaluation: whether Kiwi-Linux also moves to CEF (one engine everywhere) or stays on the WebKitGTK stack it currently ships. Open on purpose.
