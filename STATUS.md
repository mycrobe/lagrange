# STATUS

## Where we are

Phase 0 (host canvas seam) is functionally complete on both targets and
*X-ready* on real SDL2. The seam is build-level: the widget kit compiles
against stub SDL headers in `src/ui/canvas/include/`; at compile time one
of three backends provides the bodies: (1) real SDL2 via `include_next`
passthrough (LAGRANGE_CANVAS_SDL_BACKEND, the stock `app` target), (2) the
software-framebuffer shim `sdlcompat.{h,c}` (canvasapp, canvaswin), (3)
future Aqua/Toolbox backends. Touch is stubbed out for canvas builds
(`touch_stub.c`). Dev version strings: canvas targets report
`1.21.1-dev+phase0-shim (66e3824a)`; policy in AGENTS.md.

**Runtime environment (this machine, user-level)**: homebrew `sdl2` alias
= sdl2-compat (broken Retina coordinate behavior — hover/press basis
flip). Stock and canvas builds therefore use **real SDL2 2.26.5 patched
with `sdl2.26-macos-ios.diff`**, built to `/tmp/kilo/sdl2` (VOLATILE —
rebuild or move under `~/classic/` when needed; rebuild recipe = untar
SDL2-2.26.5, `patch -p1`, cmake install prefix). Kill every Lagrange/
canvasapp before launching a build (IPC steal rule, AGENTS.md).
Canvas builds use an **isolated state dir** (`/tmp/kilo/canvas-home`,
override with `CANVAS_PREF_DIR`) — never the user's real
`~/.config/lagrange`.

## Last completed milestone

**Render + input parity of the shim with real SDL2** (this session,
2026-09-09). All fixes in `src/ui/canvas/sdlcompat.{c,h}` unless noted:

1. `SDL_RenderCopy` blend semantics: blend `NONE` = straight copy (color
   mod applied, no alpha blending, dst alpha replaced, alpha mod inert);
   `BLEND` = alpha-over with linear dst-alpha (`sA + dA*(255-sA)/255`,
   was quadratic). Fixed chrome-wide text garbling.
2. `SDL_SetRenderTarget` resets the clip rect (real SDL does; a stale
   clip silently ate glyph-cache writes at 2x — "only lowercase i"
   symptom).
3. `SDL_RenderClear` honors draw-color alpha (TextBufs clear to
   `(255,255,255,0)`; the old force-opaque hack painted white boxes
   behind input-field text).
4. `SDL_TOUCH_MOUSEID` = `(Uint32)-1` (was 0 — matched every real mouse,
   so `mouseCoord_Window` returned `latestPosition_Touch()` = (0,0);
   sidebar list clicks were dead).
5. `SDL_PushEvent` fills `windowID` (0) with the last created window id —
   the app's hover tracking requires event/windowID match.
6. `SDL_ShowWindow` synthesizes SHOWN + EXPOSED window events (a fresh
   first-run window never got exposed → widget tree never drew → blank
   canvas).
7. `SDL_Delay` sleeps instead of busy-waiting (100% CPU spin).
8. `SDL_GetPrefPath` returns the isolated state dir (see above); app.c
   CANVAS `defaultDataDir_App_` = NULL so it is actually used.
9. sdlview (viewer): forwards real window events (expose/size/enter/
   leave → resize reflows work); real SDL2 is **dlopen-only** now — the
   direct link in CMake bound the app's `SDL_*` calls to the real
   library under two-level namespace, silently bypassing the shim.
   Bundle binary must be re-copied after every rebuild
   (`cp build-canvas/canvaswin build-canvas/CanvasWin.app/Contents/MacOS/`).
   Launch via `open` is currently flaky (silent death); direct shell
   launch works and `CANVAS_ERRLOG=<file>` redirects stderr for
   LaunchServices launches.
10. Canvas-main harness: `CANVAS_CLICK="x,y;x2,y2"` synthetic click
    sequence injector (points, 2s apart, `CANVAS_CLICK_AT_MS` start),
    `CANVAS_DEBUG_TEXTCACHE` in-window glyph-cache dump, texture dump
    hooks. Headless `CANVAS_CLICK` click-through-link verified
    end-to-end: link click → `document.request.started
    url:gemini://geminiprotocol.net/docs/faq.gmi`; sidebar bookmark
    click → `tabs.switch`.

Verification: `src/ui/canvas/tests/shimtext.c` (see its README) — the
glyph-cache pipeline is **bit-exact vs real SDL2** at 1x and 2x. User-
verified in the windowed viewer: all text renders (menus, tabs, URL
field, sidebar), document link clicks navigate, hover works.

## In-flight

1. **Banner headline raster** ("LAGRANGE" ASCII-art banner garbled) —
   the last known visual bug; separate from the fixed blend issues.
2. **Mac menu bar**: SDL lagrange moves menus to the native menu bar;
   shim builds show the in-window menu row instead (expected for phase
   0; note for future Aqua backend).
3. **Trackpad scroll**: wheel events verified flowing with sane deltas;
   needs user re-test with the new window-event forwarding.
4. Cleanup: diagnostics traces in sdlcompat.c/canvasmain.c are env-
   gated (`CANVAS_LOG_*`); fine to keep, but review before any commit.
   Evidence/diagnostic captures live in `/tmp/kilo/` (VOLATILE).
