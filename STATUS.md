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
field, sidebar, banner headline), document link clicks navigate, hover
works with the pointing-hand cursor, sidebar clicks act.

## In-flight

1. **Trackpad scroll too fast** — **root-caused and fixed in tree**
   (`src/ui/canvas/sdlview.c`, `src/ui/canvas/sdlcompat.c`), awaiting
   tactile confirm. Root cause was NOT a 2x pixel mismatch: the windowed
   viewer forces `CANVAS_SCALE=2` (pixelRatio 2) but the py-pixel flag
   lagrange sets in `ev.wheel.direction` (`iBit(9)` = `1u<<8`) was never
   set by sdlview, so every widget hit the *notched* wheel path and
   multiplied each small trackpad delta by `3 * lineHeight` /
   `3 * itemHeight` — massive overspeed. sdlview now, when the patched
   SDL2 reports `which==0` (precise scroll), sets
   `WHEEL_FLAG_PERPIXEL`, forwards `which`/`preciseX`/`preciseY`, and
   scales `preciseY * g_canvasScale` into canvas-pixel units — matching
   `src/platform/macos.m`. Headless `canvasapp` smoke + both canvas
   targets build clean; the real check is a trackpad scroll in
   `canvaswin` (kills/flushes the old binary first per AGENTS.md).
2. **Mac native menu (philosophy set, code NOT yet written)** — make
   `canvaswin` show a real macOS menu bar, establishing the portable
   menu-contract pattern for Aqua/Toolbox. **Decided** (do not re-litigate):
   DON'T rename the `_MacOS` menu ops (all Apple-family targets implement
   the same symbols); DON'T drag `macos.m` into the shim build (it swaps
   NSApplication delegate, installs ScrollWheel/KeyDown event monitors
   that regress the wheel path, and needs real SDL window internals the
   stub headers lack); DO a clean menu-only AppKit rewrite; keep `macos.m`
   for the legacy direct-SDL `app`. See `docs/arcana.md` → "Architecture —
   escaping SDL" for the model and why.
   **To implement:**
   - `src/ui/canvasmenu.h` (portable contract: `insertMenuItems_*`,
     `updateMenuItems_*`, `removeMenu_*`, `removeMenuItems_*`,
     `enableMenu_*`, `enableMenuIndex_*`, `enableMenuItem_*`,
     `enableMenuItemsByKey_*`, `enableMenuItemsOnHomeRow_*`,
     `handleCommand_*`, `localizeApplicationMenu_*`, `showPopupMenu_*`,
     `submenuRoot_*`, plus `hasNativeMenu_Platform()`). Declares the
     `_MacOS`-named symbols above.
   - `src/ui/canvasmenu.c` (default null backend; no-ops; portable C).
   - `src/ui/canvasmenu_impl_SDL.m` (clean AppKit backend for the SDL2
     host: build NSApp main menu from `iMenuItem` arrays, dispatch via a
     lightweight target that posts commands, enable/disable by
     command/index/key, window menu, localization; NO delegate swap, NO
     event monitors, NO SDL window coupling).
   - future `canvasmenu_impl_toolbox.c` (Menu Manager) — not now.
   - Gate collapse: change menu-using gate sites from
     `iPlatformAppleDesktop`(±`LAGRANGE_NATIVE_MENU`) and
     `LAGRANGE_MAC_MENUBAR` to a single `LAGRANGE_NATIVE_MENU` marker,
     so canvaswin uses native menus WITHOUT flipping iPlatformAppleDesktop
     (which would change fonts/DPI/layout). Sites: app.c:213/779/1635/
     1660/1792/2672/3477/4905, window.c:225/369/1668, util.c:1198/1240/
     1257/1481/4114, inputwidget.c:80, documentwidget.c:576, bindingswidget.c:148.
   - CMake: `canvaswin` gains `canvasmenu_impl_SDL.m` + `LAGRANGE_NATIVE_MENU`
     (+`LAGRANGE_MAC_MENUBAR`) defines + AppKit link; stock `app` keeps
     `macos.m`; `canvasapp` (headless) links `canvasmenu.c` and stays
     `iPlatformPcDesktop`.
   - Build order: stock `app` first (regression gate), then `canvaswin`.
     Verify native menu bar via `osascript`/System Events listing the
     running process's menu bar items (not eyeball). Linux `app` build
     stays green (contract is portable; impl is macOS-only).
   **Roadblock/risk:** the AppKit backend is ~300 fresh ObjC lines; gate
   collapse touches the working stock build, so verify it after every stage.
3. Cleanup: diagnostics traces in sdlcompat.c/canvasmain.c are env-
   gated (`CANVAS_LOG_*`); fine to keep, but review before any commit.
   Evidence/diagnostic captures live in `/tmp/kilo/` (VOLATILE).
