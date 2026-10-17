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
- Honest limits (K1 slice): no session restore, bookmarks UI, find bar, site-rules editor or hot-reload yet — the engine surfaces exist; the UI screens land with K2. Packaging waits for K3. (limits)

## K2 — Privacy center

- Per-site privacy overrides with visual indicators.
- Storage and cookie management screens.
- Import from other browsers.

## K3 — Packaging

- Linux: deb, rpm, Flatpak.
- Windows: installer and portable zip.
- Update story without silent traffic.

## K4 — Identity

- Final mascot artwork and application icon.
- Website and release channel.
