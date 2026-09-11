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
    **Remaining:** the glyph `〉` (U+3009, sidebar collapse arrow) missing from the
   bundled fontpack (`failed to find 00003009`); context-menu popups
   (`showPopupMenu_MacOS` needs an `NSEvent`, deferred); a domain-mismatch /
   untrusted cert should show a soft warning dialog rather than just an empty
   page (the on-device TOFU mismatch capture showed a blank document; the
   cert-warning banner / red-lock "soft warning" toast is still to be verified
   on tiger). The real-network
   fetch + TOFU trust gate (both states) is now evidenced on-device — see the
   "Last completed milestone". New with that slice: the Aqua host registers a
   `kAEGetURL` handler + `osx/Info.plist` declares `gemini`/`gopher`/`gophers`/
   `spartan` URL schemes (Tiger's `open` has no `--args`), and `LSEnvironment`
   `AQUA_DEBUG` so the app-owned log lands in `/tmp/L4.log`.
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
