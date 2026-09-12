# STATUS

## Where we are

**Phase 1 (host ClassicNet network seam) is complete: N1 + N2 (socket + tls + 2b
self-signed) + N3 wiring all landed and committed on `canvas-shim`.** The
host-first ClassicNet network seam is proven end-to-end (13/13 ClassicNet
host tests + the classicnet_n1/socket/tls gates + a headless frame render of
a fetched page). We are now **starting the T-tier (10.4 Tiger PPC) port**, and
per the re-sequenced order the first slice is **Phase 2
build systems + the network slice**, which is what this ledger's latest
milestone covers. Phase order stays **1) host seam → 2) Tiger/Cocoa → 3)
Classic**, with the cross-build tool systems as the parallel enabling work.

**The M-tier (OS 8/9) port — **L9**, the Classic version of lagrange — has
started on `classic-mtier` (branch cut from `canvas-shim` 2026-09-12) with
the network-first slice — the exact mirror of the T-tier `d8_smoke`.** mbedTLS-ppc (cy384 fork) is cross-built into
lagrange's own `vendor/ClassicNet/deps`, and a new `mac/` CMake project +
`scripts/build-mac.sh` cross-build the ClassicNet **Open Transport** slice
(`cn_ot` + `cn_tls` + `cn_mac_time`) + mbedTLS-ppc into `build-mac/cn_ot_smoke.bin`
(a valid PowerPC PEF console app), proving the M-tier network seam is
portable to classic Mac OS before any UI. **The on-device OT Gemini fetch is
now PROVEN on the macos9 guest (2026-09-12):** `cn_ot_smoke` did a real
Open Transport + TLS 1.2 fetch of `gemini://10.0.2.2:1965/` and got a `20
text/gemini` capsule (986 bytes, UTF-8 pool) — evidence
`logs/mtier-fetch-20260912.log` (retrieved via retrieve-log.sh).

**The T-tier fetch/TOFU slice is now evidenced end-to-end on real Tiger
hardware (2026-09-10).** The reconciled milestone: `L4.app` does a **real
ClassicNet fetch over the network on petal** (renders a live Gemini page) and
the **TOFU trust gate is exercised in both states** — a self-signed cert
whose SAN matches the host is pinned/trusted and renders; a self-signed cert
whose SAN does NOT match the host is rejected by the verify callback and the
fetch is gated (no page). Two on-device additions made this tight: (1) the
Aqua host now registers a `kAEGetURL` handler + the bundle declares
`gemini`/`gopher`/`gophers`/`spartan` URL schemes, because **Tiger's `open`
has no `--args`**, so the only way to feed a URL to a bundle-launched GUI app
is a Finder `open gemini://...` AppleEvent; and (2) `osx/Info.plist`
`LSEnvironment= AQUA_DEBUG` so the app-owned log lands in `/tmp/L4.log`
(unbuffered) instead of being discarded. See "Last completed milestone".

**The darwin8 cross-build pipeline is now standing and proven on this machine.**
The lagrange `osx/` CMake project cross-compiles the vendored ClassicNet
darwin8 slice (`cn_darwin8` BSD sockets + `cn_tls` mbedTLS + `target/d8_time.c`)
with mbedTLS-d8 and links a raw `<smoke>` into a **PPC Mach-O**, and that binary
was **shipped to petal and fetched a real Gemini page on Tiger 10.4.11** — the
network seam is confirmed portable to the T-tier. **The the_Foundation +
ClassicNet seam now cross-compiles for Tiger too**: `the_Foundation` (static,
`TFDN_CLASSICNET=ON` → its `socket.c`/`tlsrequest.c` backends) + a
`d8_tls_smoke` proof that fetches a Gemini page on petal through lagrange's
actual `iTlsRequest` path. **The Aqua canvas host now cross-builds AND RUNS** on real Tiger hardware: the
`osx/` project produces a `L4.app` bundle (PPC Mach-O) — a programmatic
single-window AppKit host (`src/macos/aquaview.m`) over the portable core +
software-framebuffer shim, linked against the `the_Foundation`/ClassicNet seam.
It displays the full window, renders Gemini content, has a working **native
NSMenu bar** and **native Cmd shortcuts**, and fetches pages over ClassicNet.
See "Last completed milestone" + In-flight for the details.

## Last completed milestone

**T-tier fetch/TOFU slice: L4 does a real ClassicNet fetch + seen both trust
states on real Tiger hardware (2026-09-10).**
`L4.app` (PPC Mach-O, this ledger's Aqua canvas host) fetches a Gemini page
over the network on petal (Tiger 10.4.11) and renders it — the whole
`gmrequest`→`iTlsRequest`→ClassicNet seam (`cn_darwin8` + `cn_tls`/mbedTLS)
in a real window, not just the seam smoke. **And the TOFU trust gate is
exercised in both directions on-device:**
- **Trusted/pinned (TOFU first-use):** a self-signed capsule cert with SAN
  `DNS:localhost`, fetched via `gemini://localhost:1966/`, is domain-verified
  + not expired → `checkTrust_GmCerts` pins it → page renders with the
  secure lock.
- **Untrusted/mismatch (gate rejects):** a self-signed cert with SAN
  `DNS:badname.invalid` fetched via `gemini://127.0.0.1:1967/` fails
  domain verification → the mbedTLS verify callback rejects the leaf → the
  handshake aborts, no page renders (the mismatch/not-verified gate).

Two on-device fixes were required. **(1) The Aqua host could not receive a
URL.** A bundle-launched GUI app on Tiger cannot take a positional URL arg:
`open --args` does not exist (Tiger's `open` treats `--args`/`-h` as a file),
and the widget kit's argv path only ever sees the `-psn_` arg. Fix: the Aqua
host registers a `kAEGetURL` AppleEvent handler (`registerUrlHandler_Aqua` in
`src/macos/aquaview.m`, posted to the widget kit as `~open newtab:1 url:%s`,
exactly like `macos.m`) and `osx/Info.plist` declares
`CFBundleURLTypes` for `gemini`/`gopher`/`gophers`/`spartan`, so a Finder
`open gemini://host/path` delivers the URL. **(2) The app-owned log was
discarded** (Finder-launched apps don't inherit ssh stdout): `osx/Info.plist`
`LSEnvironment { AQUA_DEBUG=1 }` routes stderr to `/tmp/L4.log` (the code
`freopen`s + `setvbuf` unbuffered; see arcana "Aqua host on-device run").

The capsule+fetch is driven via a host-side `gemini_capsule.py` reverse
tunneled through `ssh -R` to petal's loopback (certs for `localhost` and for
the mismatched `badname.invalid` generated on the host with openssl).

Evidence / reproduce:
- `~/classic/petal/logs/l4-aqua-tofu-git-fetch-2026-09-10.png` (real fetch:
  the live `git.skyjake.fi/lagrange/release` page, green lock).
- `l4-aqua-tofu-capsule-2026-09-10.png` (trusted self-signed capsule:
  `gemini://localhost:1966/` renders "Welcome to the ClassicNet test capsule").
- `l4-aqua-tofu-mismatch-2026-09-10.png` + `-mismatch2-...png` (untrusted
  gate: `gemini://127.0.0.1:1967/` mismatch → no page).
- `l4-aqua-tofu-runtime-2026-09-10.txt` (log: `[classicnet] TCP connect
  git.skyjake.fi:1965` / `localhost:1966` / `127.0.0.1:1967` + `TLS handshake
  OK ...`). Capsule server request log confirms the 1966 + 1967 fetches.
- Seam gate: `scripts/test-classicnet-tls.sh` still green
  (`[ca]/[tofu]/[selfgen]/[reject]` OK) — the seam-level TOFU gate is intact.
- `build-host` stock `app` still builds (regression gate).

Deploy/run recipe (per-machine detail in `~/.local/share/doc/petal.md` + the
arcana "Aqua host on-device run"): build via `scripts/build-osx.sh` inside
the amd64 darwin8 container → assemble `L4.app` with the repo `osx/Info.plist`
+ `build-osx/L4.app/.../resources.lgr` → `scp -r` to `/tmp/L4.app` on petal →
`open /tmp/L4.app` (LaunchServices) → `open gemini://localhost:1966/` to
inject a URL. The single-instance IPC means a fresh instance only runs on a
clean launch; kill by PID (`pkill -9 -f /tmp/L4.app` may miss the truncated
cmdline) between runs.

## In-flight

0. **Multiple windows for L4 on Tiger (2026-09-11) — cross-built + on-device
   verified; a close-path crash found and fixed on device.** The Aqua host was
   effectively single-window: it presented only shim window index 0 AND tagged
   every Aqua mouse event with `g_windows[0]->id`, so an extra window (Cmd+N →
   `newExtra_Window`) never got its own native window or input routing.  Fixed
   end-to-end:
   **Shim (`sdlcompat.c/.h`)** —    (a) `SDL_RenderPresent` now calls
   `presentHook_` with the **presenting window's windowID** (not hardcoded 0;
   not an array index — indices unstable because popup/menu windows share
   `g_windows` and `SDL_DestroyWindow` swaps-last-into-place); (b) new
   `setWindow{created,destroyed,title}Hook_canvas`; (c) new
   `canvasPixelsByWindowId_canvas`.  **Rendering regression found on device
   and fixed:** `SDL_CreateRenderer` never assigned `renderer->window`, so
   `r->window->id` was `0` and every present landed on `aqFind_(0)` → −1 → no
   blit → black window (title bar still set via the title hook).  Added
   `d->window = win; (sdlcompat.c)`.  **Aqua host (`aquaview.m`)** — one
   `NSWindow`+`AquaCanvasView` per shim window, keyed by windowID in a compact
   table; mouse/key/scroll/text events tagged with the originating window's
   shim id; `becomeKey/resignKeyWindow` post
   `SDL_WINDOWEVENT_FOCUS_{GAINED,LOST}`; title hook updates the native title
   bar; per-window dirty-gate preserves the idle-CPU fix; popup/dropdown
   windows (`SDL_WINDOW_POPUP_MENU`/`SKIP_TASKBAR`) carry **no** native
   window.  Primary-window close still quits; closing an extra window posts
   `SDL_WINDOWEVENT_CLOSE`.
   **Crash found on device (2026-09-11):** `windowDestroyedAqua_` called
   `[win close]`, which re-entered the `windowWillClose:` delegate and (for the
   primary window) `quitRequested` → `[NSApp terminate:]` from inside the
   timer-tick frame that was tearing down the window model — a double release
   (`EXC_BAD_ACCESS` in `objc_msgSend`, crash log `L4.crash.log`, `delete_MainWindow`
   → `windowDestroyedAqua_`).  Fixed by using `[win orderOut:]` (no
   `windowWillClose:` re-entry) + clearing the freed table slot so a stale
   lookup can't re-hit a released object.  **Verified on petal:** clean
   single-window start (cleared stale `state.lgr` — a 12-window pile-up from
   the pre-fix crash era, not a feature bug) → File ▸ New Window → 2 distinct
   windows (separate titles) → Close Tab → back to 1, **no crash**; close of
   the primary main    window (the delete_MainWindow path that crashed) → **no new
   crash** (app exits cleanly after the last window).  After the
   renderer-window fix, both windows render full page content
   (`l4-multiwin-render-2026-09-11.png`).  Evidence:
   `~/classic/petal/logs/l4-multiwin-two-2026-09-11.png` +
   `l4-multiwin-2026-09-11.png` + `l4-render-fixed-2026-09-11.png` +
   `l4-multiwin-render-2026-09-11.png`.  Open concern: `setActiveWindow_App` is
   only driven by *extra*-window focus events (window.c `FOCUS_GAINED` returns
   iFalse for main windows) — confirm on device that focusing back the primary
   window routes input correctly.

1. **L4 ▸ About as a native dialog + detached Preferences window (2026-09-12).**
   **About:** the app menu's "About Lagrange" item no longer posts
   `!open ... url:about:lagrange` (a page tab); it targets the Aqua app
   delegate's `showAboutDlg:` (`canvasmenu_impl_aqua.m` + `aquaview.m`), which
   shows a small native "About lagrange" window with the dev version string
   (`LAGRANGE_APP_VERSION`, e.g. "Version 1.21.1-dev+L4-tiger (sha)") and
   author — the real macOS menu behaviour.  Verified on petal:
   `l4-about-dialog-2026-09-12.png`.  **Preferences:** was forced in-window by
   `app.c` under `LAGRANGE_CANVAS` ("a detached window would be invisible").
   Now gated on `LAGRANGE_AQUA` (osx L4 `target_compile_definitions`), so the
   knobs/prefs dialog is promoted to its own **extra window** via
   `promoteDialogToWindow_Widget` (which `newExtra_Window`s it — natively
   rendered by the multi-window Aqua host).  **Second close-path crash found +
   fixed on device:** closing the detached Preferences window (title-bar close
   button) crashed `windowDestroyedAqua_` (objc_msgSend on a freed window). 
   Root cause: the shim `AquaWindow`s default to `releasedWhenClosed` **YES**, so
   AppKit deallocates the window on a user close *before* the next-tick shim
   destroy — the destroy hook messages a freed NSWindow/NSView.  Fix:
   `[aq setReleasedWhenClosed:NO]` on every backend-created window (holds it
   until the destroy hook's one balanced `release`; same pattern as the About
   window).  Verified on device: About opens (no crash), New Window + Window ▸
   Close Window work, no new crash.  ⚠️ The exact *prefs close-button* path could
   NOT be driven over AX on petal (System Events close-button/Preferences clicks
   time out — NSReceiverEvaluationScriptError 4; a separate L4 main-thread AX
   responsiveness concern worth a look) — **needs a real mouse close of the
   Preferences window to confirm the fix.**
   L4 idles at **~25% (measured 23-30%) CPU on petal's G4 doing nothing** —
   a full-window 60Hz rerender that shouldn't happen. On-device `sample` is
   decisive: in a 5s capture **291/312 timer ticks call `step_App`, of which
   286 hit `refresh_App` → `draw_MainWindow`** (154 `SDL_RenderPresent` =
   full-canvas blit → `CGContextDrawImage`, 105 `drawRoot_Widget` =
   widget-tree re-render); only 5/312 process events. The app redraws the
   whole document ~55×/s at idle. **Root cause (traced to the portable core,
   not the backend):** `refresh_App`'s draw gate (app.c:2989/2993) only opens
   when `pendingRefresh`/`isRefreshPending` is set, and the only setter is
   `postRefresh_Window` (app.c:3161) — invoked from `refresh_Widget`
   (widget.c:2743) **and** from `addTicker_App` itself (app.c:3383/3389). So
   any ticker that re-registers every frame keeps the gate open. Prime
   suspect: `animate_DocumentWidget` (documentwidget.c:697-709) re-arms itself
   while `swipeView` is live or `linkInfo`/`sideOpacity`/`altTextOpacity` anim
   is unfinished. (Ruled out: `input.blink` — gated by `selected_WidgetFlag`
   so it does NOT fire without editing input, and it's 500ms anyway, not 60Hz.)
   **Why it's not "just slower SDL" — it's the model.** The real SDL2 host
   runs `run_App_` (app.c:2912) → `step_App(waitForNewEvents_AppEventMode)`,
   which *blocks* in `nextEvent_App_` (app.c:2308) `SDL_WaitEvent` — the loop
   sleeps at ~0% CPU until an event arrives, and `isWaitingAllowed_App_`
   (app.c:2278) still draws first if a refresh is pending. The Aqua host can't
   block the main thread (`[NSApp run]` owns it), so `aquaview.m` substitutes a
   free-running ~60Hz `NSTimer` → `step_App(postedEventsOnly_AppEventMode)`
   (`SDL_PollEvent`, app.c:2344 — a busy poll). So `step_App` runs 60×/s, and
   the re-arming ticker guarantees `refresh_App`'s gate is open most ticks →
   `draw_MainWindow` on ~286/312. Two independent causes: the poll-when-idle
   model **and** a ticker that never lets the scene go quiet.
   **Fix levers (both needed):** (1) portable core — stop the ticker
   self-re-arm when content is static (fix the `animate_DocumentWidget` OR
   condition / make the anims actually reach `isFinished_Anim` at rest, and
   don't have `addTicker_App` unconditionally post a refresh); (2) Aqua
   backend — make the tick block/coalesce instead of busy-poll (an
   `SDL_WaitEventTimeout`-style short dequeue so the timer parks when idle,
   matching SDL's block-on-event semantics), and gate
   `presentHookAqua_` (aquaview.m:437-438) so a full `setNeedsDisplay` +
   synchronous `displayIfNeeded` blit only happens when a region was actually
   dirtied. On a G4 the 154 `SDL_RenderPresent` full-frame copies dominate.
   **DECISION: do NOT move the render to another thread (2026-09-11).** petal
   is a single-core G4 (`sysctl hw.ncpu` = 1), so off-threading the current
   render would only relocate the same wasted work and add cross-thread state
   sync + an extra framebuffer copy — more CPU, more complexity, ~0 win.
   Threaded shaping/rasterization is a legit *latency* optimization for a
   1-core machine (pipeline two frames, worker does the heavy text shaping,
   double-buffer + semaphore), but (a) the widget kit is not written to draw
   off the main thread (`step_App` mutates the widget tree during draw, so it
   needs a real off-main-thread-draw rework), and (b) it wouldn't reduce idle
   CPU — the waste is in the loop model, not the shaping cost. Fix the idle
   redraw first (levers 1+2); revisit threading only if interactive latency is
   still bad. Evidence: `sample 3448` on petal (`l4-perf-sample-2026-09-11.txt`)
   + `ps` CPU. **Lever 2 (Aqua backend, 2026-09-11) now implemented in
   `aquaview.m`:** (a) the free-running 60Hz repeating nil-target timer is
   replaced by a self-rescheduling one-shot that **coalesces** — it parks to a
   0.25s interval once the scene is static for a couple of frames, and returns
   to 60Hz the moment a real change (dirty frame) or a native input event
   (mouse/key/scroll/URL/quit, via `wakeAquaTick_`) needs prompt service; (b)
   `presentHookAqua_` is **dirty-gated** — `setNeedsDisplay` + the synchronous
   `displayIfNeeded` (a full-canvas `CGContextDrawImage`) only runs when the
   shim framebuffer actually differs (memcmp) from the last presented frame, so
   a re-arming widget ticker that redraws identical pixels no longer blits
   55×/s. The tick still always calls `step_App` when it runs, so queued SDL
   events/commands (network completions) are drained at the slow rate and any
   resulting change immediately bumps back to 60Hz. Cross-syntax-checked with
   the real darwin8 PPC gcc (exit 0, no new warnings) **and the full L4.app
   cross-build + deploy to petal ran clean (2026-09-11)**: on-device settled
   idle CPU is now **~0.2–0.4%** (was 23–30%; the 60% transient during the
   active fetch/render drops once the page settles) — the window renders the
   live `git.skyjake.fi/lagrange/release/tags/` page correctly and there are
   **0 double-free errors**. One new on-device fix: the self-rescheduling timer
   needed a `retain` (`timerWithTimeInterval:` returns +0 autoreleased; holding
   it in `gPendingTimer_` and releasing as if owned caused a malloc double-free
   flood, now fixed in `armAquaTick_`). **Lever 1 (portable core ticker re-arm)
   is still open** — deferred per 2026-09-11; the idle-CPU problem appears
   resolved by the backend alone, so lever 2 is now the priority and lever 1
   will be revisited only if a real animation/scrolling path still misbehaves.
1. **Aqua canvas host — cross-build DONE, archive blocker RESOLVED, and the
   **T-tier app now RUNS on real Tiger hardware** (2026-09-10).** The T-tier
   product is named **L4** ("a Lagrange point for 10.4"), replacing the
   placeholder `Gemini` bundle/target name (`osx/CMakeLists.txt`, `osx/Info.plist`,
   `osx/PkgInfo`); the menu bar shows `L4`. `src/macos/` (single window, `NSView`
   blit, shim event translation) builds into `L4.app` (PPC Mach-O) and, launched
   on petal (Tiger 10.4.11), **displays the full window and renders a Gemini
   page** — sidebar + URL bar + bookmarks + the `getting_started.gmi` content —
   proving resources.lgr (the_Foundation), the widget kit, and the AppKit host
   all work end-to-end. **The resource-load blocker was fixed:** an
   endianness-guard typobug in `the_Foundation`'s `stream.c` (tested `iBigEndian`,
   never defined; the generated config.h carries `iHaveBigEndian`), so big-endian
   PPC read the little-endian ZIP byte-swapped and `readDirectory_Archive_()`
   failed. Fix + root-cause in docs/arcana.md ("Aqua host on-device run").
   A second on-device fix: Finder/LaunchServices injects a `-psn_<serial>`
   argument into GUI apps; the portable CommandLine parser rejects it ("Unknown
   option: p") and terminates, so `aquamain.c` strips `-psn_` args before
   `run_App` (arcana). On-device evidence:
   `~/classic/petal/logs/l4-aqua-ondevice-2026-09-10.png` (window) +
   `l4-aqua-runtime-2026-09-10.txt` (log); the archive proof is
   `lagrange-d8-archive-tfdn-2026-09-10.txt`.
   **Native OS menu bar now works and is merged (2026-09-10):** the Aqua host
   runs AppKit's `[NSApp run]` and steps the widget kit from a 60Hz timer
   (`app.c` `step_App`/`beginAppEventLoop_App`/`init_App`/`deinit_App_Instance`/
   `isAppRunning`, `aquaview.m` `runAquaMainLoop`) — that is what makes the menu
   bar render at all; the 10.4-appkit `canvasmenu_impl_aqua.m` builds the app +
    Window menus and the widget kit's File/Edit/View/Bookmarks/Identity/Help, and
    the app-menu item is titled `L4`. ⚠️ The "AppKit merges it into a single app
    menu" assumption was WRONG on Tiger (verified on petal): pre-10.6 AppKit does
    not identify a programmatically-set `mainMenu[0]` as the app menu, so it
    prepends its own synthesised bold app-name `L4` and the backend's `L4` shows
     up as a second, plain menu (two "L4" in the bar). **FIXED + PROVEN ON-DEVICE
     (2026-09-10):** nominate the index-0 submenu with the hidden
     `-[NSApplication setAppleMenu:]` (`canvasmenu_impl_aqua.m`) — the rebuilt
     `L4.app` now shows a single bold `L4` app menu next to the Apple logo
     (About/Preferences/Hide/Quit), no duplicate, and still renders the Gemini
      page. Evidence: `~/classic/petal/logs/l4-aqua-menufix-2026-09-10.png` +
      `-...txt` (vs the bug `l4-aqua-menubug-2026-09-10.png`); root cause in
      docs/arcana.md. **The `_NSAutoreleaseNoPool` flood is also GONE (proven
      on-device, 2026-09-10):** `[NSApp run]`+timer under Tiger has no automatic
      pool per event, so `main()` now wraps its body in an NSAutoreleasePool frame
      (`begin/endAutoreleasePool_Aqua`, exposed to C `aquamain.c`) and `tick:` uses
      a per-frame pool; the app-owned log is unbuffered (`setvbuf` `_IONBF`) so the
      `[aqua]` stage markers flush (they were sitting in a 4KB buffer, lost on a
      crash). 614 leak lines before → 0 (evidence
      `~/classic/petal/logs/l4-aqua-poolfix-2026-09-10.txt`); details in
       docs/arcana.md. **Mouse *delivery* fixed (2026-09-10):** Tiger swallows the
       first click on a non-key window as an activation click (`acceptsFirstMouse`
       defaults NO) — the window's key status isn't established before the first
       click in the `[NSApp run]`+timer model, so clicks never reached the view.
       `-$acceptsFirstMouse` YES on `AquaCanvasView`; also `sdlPoint`/`drawCanvasInto`
       now share a `canvasRectInView` letterbox transform so mouse coords map to the
       shim canvas even when the window is resized. **Mouse interaction now FULLY
       works on petal (2026-09-10):** the remaining "clicks reach the document but
       `view->hoverLink` stays NULL, so links never open" blocker was a **window-ID
       routing bug in the shim**, not the event loop (the earlier `[NSApp run]`+timer
       regression hypothesis was wrong). `SDL_PushEvent()` tags an untagged mouse
       event with `g_nextWindowId` — the *last created* window. Lagrange creates
       several windows at startup; only window index 0 is presented (and thus
       visible — `SDL_RenderPresent` calls `presentHook_(0)` and the Aqua host blits
       index 0). So every Aqua mouse event was routed to an **empty hidden window**,
       where the hovered `DocumentWidget` had `size.y == 0` / `visibleLinks == 0`, so
       `hoverLink` could never be set. **Key events were unaffected** (they carry no
       window filter), which is exactly why the menu + Cmd shortcuts worked while the
       mouse appeared dead. Fix: `SDL_PushEvent` tags untagged events with
       `g_windows[0]->id` (the presented/main window) instead of `g_nextWindowId`.
       Proven on petal with a gated in-app synthetic-mouse self-test
       (`AQUA_SELFTEST="x,y"`, needed because Tiger's WindowServer drops CGEvent
       posts from an SSH session): the move reaches the visible document
       (`visibleLinks n=2`), `hoverLink` is set, and a click opens the link (status
       area shows the hovered URL). Root cause + diagnostic recipe in docs/arcana.md.
       **Native Cmd shortcuts work** via menu-first key-equivalent handling +
    a widget-kit fallback in the Aqua window; quitting is clean (`atexit`
    `deinit_Foundation` so the AppKit-exit host doesn't trip the Foundation
     assert). Evidence: `~/classic/petal/logs/l4-aqua-native-menu-2026-09-10.png`.
     **Menu ellipsis fixed (2026-09-11):** the app menu "Preferences…" was
     rendering as `Preferences‚Ä¶`. Root cause is a compiler charset bug, not a
     Tiger limit — the darwin8 gcc decodes ObjC `@"..."` literals as MacRoman,
     mangling any non-ASCII byte (and `@"\uXXXX"` escapes). Fixed by building
     such titles from UTF-8 at runtime via `stringWithUTF8String` (new
     `utf8String_` helper in `canvasmenu_impl_aqua.m`), also fixing the arrow
     key-equivalents (`@"\u2190"` etc.). Proven on petal with a PPC probe; the
     rebuilt L4 binary carries the correct UTF-8 `Preferences…`. Full root
     cause + rule in docs/arcana.md ("darwin8 gcc mangles non-ASCII...").
     **Remaining:** the glyph `〉` (U+3009, sidebar collapse arrow) missing from the
    bundled fontpack (`failed to find 00003009`); a domain-mismatch /
    untrusted cert should show a soft warning dialog rather than just an empty
    page (the on-device TOFU mismatch capture showed a blank document; the
    cert-warning banner / red-lock "soft warning" toast is still to be verified
    on tiger). The real-network
    fetch + TOFU trust gate (both states) is now evidenced on-device — see the
    "Last completed milestone". New with that slice: the Aqua host registers a
    `kAEGetURL` handler + `osx/Info.plist` declares `gemini`/`gopher`/`gophers`/
    `spartan` URL schemes (Tiger's `open` has no `--args`), and `LSEnvironment`
    `AQUA_DEBUG` so the app-owned log lands in `/tmp/L4.log`.
    **Context-menu popups now implemented (2026-09-11):** right-click
    context menus actually appear on L4.  Tiger (10.4) does not have the
    10.6+ `popUpMenuPositioningItem:atLocation:inView:`, so the native menu
    backend must pop through `+[NSMenu popUpContextMenu:withEvent:forView:]`,
    which needs the originating NSEvent.  The Aqua view now remembers the most
    recent mouse-down NSEvent (`setAquaPopupEvent_Aqua` in aquaview.m, exposed
    C-clean via aquaview.h) and `showPopupMenu_MacOS` uses it + `aquaMainView_`
    as the `forView`.  The tick timer is scheduled in `NSDefaultRunLoopMode`,
    so it does not re-enter the widget kit during the modal menu-tracking loop
     (no pausing needed).  Cross-gcc clean + full L4 rebuild + on-device launch
     verified; the actual right-click pop needs a physical click on petal
     (System Events is gated over SSH).
     **Native context-menu glyphs fixed (2026-09-11):** Tiger's menu font can't
     render Lagrange's `*_Icon` codepoints (tofu), so the document menu's
     "Go Back / Go Forward / Go to Parent / Go to Root" nav items showed as
     `>>> ◼` and any icon-prefixed label as a black box.  `showPopupMenu_MacOS`
     + `populateMenu_` now run labels through `nativeMenuLabel_` (strips the
     `###`/`///`/``` markers, the leading icon glyph + space, and colour
     escapes — the same text-only treatment the real mac host uses), and
     `documentwidget.c`'s nav items reach their text-bearing labels on this host
     via `|| defined (LAGRANGE_NATIVE_MENU)` (the `iPlatformApple` gate isn't
      set on the darwin8 cross-build, and enabling it globnally risks Apple
      return-key semantics).  Cross-gcc clean + rebuilt + deployed (running on
      petal, PID 4634); verify by right-clicking the document.
      **Native checkmarks for checked items (2026-09-11):** `nativeMenuLabel_`
      now reports the `###` (checked) marker and the item builders set
      `[item setState:NSOnState]` for it (menu bar + context menu), so checked
      items get a real macOS checkmark instead of a stripped-to-plain-text
      label (mirrors src/platform/macos.m).  Lagrange marks items `###`
      dynamically (context menus via `updateMenuItems`/`setSelected_`
      `NativeMenuItem`, dropdown menu buttons), so the checkmark shows there;
      the top-level menu-bar sidebar-mode toggles are static (unmarked) under
      the native-menu path — same as the real mac build's bar.  No default
      checked       item is reachable to screenshot via SSH, so verified by
      cross-gcc clean + rebuild + deployed (running on petal) + `setState:`
      selector present in the binary.
      **Menu-bar checkmarks too (2026-09-11):** the View menu's left-sidebar-mode
      toggles ("Show Bookmarks / …") now get a native checkmark on the active
      one, updating live.  The native menu bar is built once from static arrays
      (the widget kit's `###` marking only runs in the open/dropdown path), so
      the Aqua host mirrors it itself: `handleCommand_MacOS` handles
      `sidebar.mode.changed` and `markSidebarModeCheck_MacOS` walks the View
      menu's `sidebar.mode arg:N toggle:1` items, setting `NSOnState` on the
      matching one.  The initial state required the sidebar to announce its mode
      on init (`sidebarwidget.c` now posts `sidebar.mode.changed arg:N` after
      the initial `setMode`, queued so it's processed once the menu bar exists);
      `root.c`'s toolbar handler was made NULL-safe for that.  Verified on-device:
      "Show Bookmarks" checked at launch, then live-moved to "Show Feed Entries"
      after clicking it (evidence
      `l4-aqua-viewmenu-check-bookmarks-...png` + `-feeds-...png`).
 2. **N3 tactile confirmation (Phase 1) is now largely covered by the T-tier
    fetch/TOFU evidence above** — the visual check that the app shows a fetched
    page + the TOFU trust/mismatch UI, on petal, was produced for the Aqua host
    (`L4.app`). For the SAME canvasview path the *host* N3 target (`canvaswin`)
    fetched/render is already proven headless; a windowed canvas confirmation on
    QEMU/petal is still the residual.
3. **M-tier mandatory the_Foundation C libs — standing (2026-09-10).**
   `deps/libunistring-retro68` (iconv-free, `U iconv*` clean;
   `scripts/setup-libunistring.sh`) plus the other two mandatory GNU C libs:
   **PCRE2 10.47** (`deps/pcre2-retro68`) and **zlib 1.3.1** (`deps/zlib-retro68`,
   inflates `resources.lgr`) via **`scripts/setup-mtier-libs.sh`**.
   Each is verified Retro68 PPC with the core symbols (`pcre2_compile_8`/`match_8`,
   `inflate`/`deflate`/`crc32`/`zlibVersion`). Gotchas (arcana "the_Foundation
   classic C libs"): PCRE2 `int32_t==long` pointer mismatch (build with
   `-Wno-error=incompatible-pointer-types`) + `pcre2grep`'s `<io.h>` (install libs
   + headers only); zlib's macOS `AR=libtool` makes a 96-byte EMPTY archive (use
   `AR=powerpc-apple-macos-ar ARFLAGS=rc`). **Text shaping is now built for
   Classic too:** **HarfBuzz 2.8.2** (`deps/harfbuzz-retro68`) + **FriBidi 1.0.13**
   (`deps/fribidi-retro68`) via **`scripts/setup-harfbuzz-fribidi.sh`** — HarfBuzz
   meson cross-build with `HB_NO_MT` (Retro68 lacks pthread_mutex_*) and
   glib/freetype/icu/cairo all OFF (lagrange needs only the default
   `hb_font_funcs`; it rasterizes via stb_truetype); FriBidi library only (its CLI
   fails on Retro68). Arcana "the_Foundation classic C libs". mbedTLS-ppc is
   de-scoped (proven in starscape on OS 9). Next: point the Retro68
   the_Foundation build's `UNISTRING_DIR`/PCRE2/zlib dirs here, and wire
   `harfbuzz-retro68`/`fribidi-retro68` into the Classic renderer target
   (`LAGRANGE_ENABLE_HARFBUZZ=1` + `LAGRANGE_ENABLE_FRIBIDI=1`, link
   `libharfbuzz.a`/`libfribidi.a`).
 4. **the_Foundation shim migration** — the darwin8 shims
    (strnlen/clock_gettime/pthread_setname_np/posix_spawn/AVOID_ANY_THREADS)
    are build-time local for now (`osx/*`); migrate into the_Foundation's
    `src/platform/apple.c`/`posix/` behind an OS-version check on the
    `classicnet-seam` branch so they're reusable by the Retro68/M-tier flavor.
 5. ClassicNet submodule pin: lagrange is at `f1dbf66` (a local `darwin8-transport`
    commit adding the `[classicnet]` stderr markers — must be pushed for a clean
    clone). The 6-arg `CN_TlsCreate` stays until starscape's client-cert identity
    commit (`57ca5db`) lands on origin; then bump + migrate to the 10-arg form
    as part of the Gemini auth work.
 6. **M-tier network-first slice — PROVEN end-to-end on the macos9 guest
    (2026-09-12, `classic-mtier`).** The M-tier analog of the T-tier
    `d8_smoke` is up: `mac/CMakeLists.txt` + `scripts/build-mac.sh`
    cross-build the vendored ClassicNet OT slice (`cn_ot` Open Transport +
    `cn_tls` mbedTLS + `cn_mac_time` platform glue) + cy384's mbedTLS-ppc
    into `build-mac/cn_ot_smoke.bin` (Retro68, valid PPC PEF console app)
    — clean cross-build. mbedTLS-ppc was built into lagrange's own
    `vendor/ClassicNet/deps` (previously only present via starscape's copy)
    via the vendored `setup-mbedtls.sh` PPC step; pinned fork commit
    `01162ec6`. `cn_ot_smoke.c` drives the CNTransport poll/send/recv vtable
    over OT with `YieldToAnyThread()` between would-block polls (the OT
    async notifiers need it) and a TickCount watchdog (45 s), the starscape
    `gm_session.c` pattern. **On-device: proven** — deployed the `.APPL`
    (not the `.bin`) to Startup Items with a real creator (`CnOs`) + a SIZE
    resource, booted the macos9 clone headless, and the app fetched
    `gemini://10.0.2.2:1965/` over OT+TLS 1.2 → got a `20 text/gemini`
    capsule (986 bytes, UTF-8 `café/naïve`), logged to the boot root and
    pulled with retrieve-log.sh → `logs/mtier-fetch-20260912.log`.
    The LaunchAPPL-stdout route is a dead end here (see arcana); the proven
    on-device path for a console app is Startup-Items + boot-root log +
    retrieve-log.sh, and the ONLY reason it can launch at all is the real
    creator (a `.bin`/`????` deploy → Finder error -199).
