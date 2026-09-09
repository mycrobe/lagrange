# STATUS

## Where we are

**Next: Phase 1 — ClassicNet network seam (host-first); phase order re-scoped
(2026-09-09).** Remaining work is ordered **1) ClassicNet on the host →
2) Tiger/Cocoa → 3) Classic**; the cross-build tool systems are the parallel
enabling work (only host mbedTLS + ClassicNet's host slice are needed for
step 1). The seam reuses the "escape SDL" pattern for networking: keep
`gmrequest.c`/`gmcerts` untouched and reimplement the_Foundation's
`iSocket`/`iTlsRequest` over ClassicNet's `CNTransport` vtable (`cn_darwin8`
host/Tiger, `cn_ot` Classic, `cn_tls` mbedTLS, `CN_TLS_FORCE_TLS12=1`) behind
the identical public API, with a `LAGRANGE_CLASSICNET` compile-time switch.
Host-verified: ClassicNet's own host slice builds and **13/13 tests pass on
this Mac** (incl. `test_darwin8`, the transport, and `test_h2_download`, real
I/O); host mbedTLS (`mbedtls-host3`) is present. Plan: **N1** vendor ClassicNet
host slice + host mbedTLS + a real-fetch smoke test → **N2** the
`Socket`/`TlsRequest` seam → **N3** wire into `canvaswin` so the viewer
actually fetches a Gemini page.

**Typography is parked** (was a host-testable Phase-1 item). Classic renders
**grayscale AA** (CopyBits has no per-pixel alpha → a software src-over
composite over the GWorld buffer; see arcana); the 1-bit "Platinum-sharp" UI +
pixel-aligned bitmap fontpack + the per-spec `smooth` machinery are deferred
nice-to-haves. AA is the default, so no typography groundwork is needed for
correctness.

The Phase-0 viewer now defaults to a 1x (non-Retina) rasterization
(`canvasmain.c`): `CANVAS_SCALE=1` → `pixelRatio=1`, and the HighDPI window
still presents it at an integer 2x (crisp 2x2 blocks), to validate the 1x
Cocoa/classic targets on this 2x-display Mac.

Phase 0 (canvas seam) is functionally complete on both targets and *X-ready*
on real SDL2, and `canvaswin` now shows a **real native macOS menu
bar** (the Ph0.5 menu-contract milestone). The seam is build-level: the
widget kit compiles against stub SDL headers in `src/ui/canvas/include/`; at
compile time one of three backends provides the bodies: (1) real SDL2 via
`include_next` passthrough (LAGRANGE_CANVAS_SDL_BACKEND, the stock `app`
target), (2) the software-framebuffer shim `sdlcompat.{h,c}` (canvasapp,
canvaswin), (3) future Aqua/Toolbox backends. Touch is stubbed for canvas
builds (`touch_stub.c`). Menus are a second escape-from-SDL seam: the
portable contract lives in `src/ui/canvasmenu.h` and each target supplies a
`_MacOS`-family implementation (AppKit `canvasmenu_impl_SDL.m` for the SDL2
host, `macos.m` for the legacy direct-SDL `app`, `canvasmenu.c` null no-ops
for headless). Both stock `app` and both canvas targets build clean with
`LAGRANGE_NATIVE_MENU` as the single compile-time menu marker (menus no
longer keyed off `iPlatformAppleDesktop`).

**Runtime environment (this machine, user-level)**: homebrew `sdl2` alias
= sdl2-compat (broken Retina coordinate behavior). Stock and canvas builds
therefore use **real SDL2 2.26.5 patched with `sdl2.26-macos-ios.diff`**,
built to `/tmp/kilo/sdl2` (VOLATILE — rebuild or move under `~/classic/`
when needed). Kill every Lagrange/canvasapp before launching a build (IPC
steal rule, AGENTS.md). Canvas builds use an **isolated state dir**
(`/tmp/kilo/canvas-home`, override with `CANVAS_PREF_DIR`).

## Last completed milestone

**Native macOS menu bar on `canvaswin`** (this session, 2026-09-09). New
`src/ui/canvasmenu.h` (portable contract), `canvasmenu.c` (null backend),
`canvasmenu_impl_SDL.m` (clean AppKit backend — builds the NSApp main menu
from `iMenuItem` arrays, dispatches via a lightweight `MenuCommands` target
that posts commands, enable/disable by command/index/key, window menu +
localization; **no** delegate swap, **no** event monitors, **no** SDL window
internals). `macos.h` now pulls menu declarations from `canvasmenu.h`; a
`hasNativeMenu_Platform()` was added to `macos.m` and the null/AppKit
backends. Menu gate sites were collapsed from
`iPlatformAppleDesktop`(±`LAGRANGE_NATIVE_MENU`) / `LAGRANGE_MAC_MENUBAR` to
a single `LAGRANGE_NATIVE_MENU` marker (app.c, window.c, util.c,
inputwidget.c, documentwidget.c, bindingswidget.c, root.c); defs.h suppresses
the in-window `LAGRANGE_MENUBAR` when the native menu is present. CMake:
`canvasapp` links `canvasmenu.c`; `canvaswin` links `canvasmenu_impl_SDL.m`,
gets `LAGRANGE_NATIVE_MENU`(+`LAGRANGE_MAC_MENUBAR`) defines and AppKit on
Apple. Stock `app` keeps `macos.m`.

Also fixed the observed host-scaling issue: `canvaswin` used to stretch the
shim canvas to the view window non-uniformly (vertical stretch). `sdlview.c`
now letterboxes and uses a whole-number scale so the 2x canvas maps to whole
window pixels (crisp text even though `SDL_SetWindowSize` is a no-op in the
shim, i.e. the canvas is locked to the app window's 900×560@2x logical size).

Evidence / reproduce:
- Menu bar via System Events (not eyeball): `osascript -e
  'tell application "System Events" to tell (first process whose name
  contains "canvaswin") to get name of every menu bar item of menu bar 1'`
  → `Apple, canvaswin, File, Edit, View, Bookmarks, Identity, Window, Help`.
- `File` submenu populated (New Window/New Tab/Open Location…/Close Tab/
  Save to Downloads…/Preferences…/Quit Lagrange); `Identity` →
  `New Identity…` opened the create-identity dialog end-to-end (command
  dispatch via `postCommand_Root`).
- Build: `cmake --build build-canvas --target canvaswin canvasapp -j8`
  (after `cmake -S . -B build-canvas`). Launch:
  `CANVAS_PREF_DIR=/tmp/kilo/canvas-home CANVAS_ERRLOG=/tmp/kilo/cw.err
  ./build-canvas/canvaswin --canvas-window --canvas-frames -1`.
- Default window (900×560) is exactly 1:1 with the canvas (`viewOut=1800x1120`
  = `canvas=1800x1120`); a resized/`--canvas-view` window letterboxes crisply.
- Regression gate: `cmake --build build-host --target app -j8` stays green
  (stock `macos.m` app).

## In-flight

1. **Trackpad scroll confirm** (root-caused/fixed in tree, awaiting tactile
   confirm) — sdlview sets `WHEEL_FLAG_PERPIXEL` + forwards `preciseX/Y`
   scaled by `g_canvasScale` when the patched SDL2 reports `which==0`.
2. **Host-scaling polish** — letterbox+integer scale works and text is crisp,
   but the shim canvas cannot follow window resize (`SDL_SetWindowSize` is a
   no-op in `sdlcompat.c`), so a resized view window letterboxes rather than
   refilling. If a resize-follow behavior is wanted, wire
   `SDL_SetWindowSize` → canvas resize in `sdlcompat.c`.
3. **Multi-window** — **decided**: NOT needed for the Phase 0 host (declined;
   `app.c` forces `detachedPrefs=iFalse` on `LAGRANGE_CANVAS` so dialogs are
   in-window sheets). REQUIRED for the future Aqua/Toolbox backends: one OS
   window per shim `iWindow`, positioned from the app window rect, events
   routed by shim window **id** (not index — the shim swap-removes indices on
   close). Earlier viewer-mirror prototype regressed (redraw/event spin +
   index instability), so it was reverted; the requirement is recorded in
   `docs/arcana.md`.
4. Cosmetic: canvaswin menu puts `Preferences…`/`Quit` in the `File` menu
   (`LAGRANGE_PC_MENUS` gated on `iPlatformPcDesktop`); on the stock mac app
   they live in the System menu. Suppressing `LAGRANGE_PC_MENUS` for
   `LAGRANGE_NATIVE_MENU` is possible but deferred — the SDL default app-menu
   slots would need verification first.
5. Cleanup: diagnostics traces in sdlcompat.c/canvasmain.c are env-gated
   (`CANVAS_LOG_*`); fine to keep, review before any commit. Evidence lives
   in `/tmp/kilo/` (VOLATILE).
