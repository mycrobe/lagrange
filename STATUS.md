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

**Aqua (Tiger/Leopard PPC) canvas host: cross-build → `L4.app` and RUNS on real
Tiger hardware with a native menu bar (2026-09-10).**
A programmatic single-window AppKit host (`src/macos/`) cross-compiles and links
into a **PPC Mach-O** `L4` inside a `L4.app` bundle — the whole portable core +
widget kit + software-framebuffer shim against the `the_Foundation`/ClassicNet
seam. Launched on petal (Tiger 10.4.11) it shows the full window, renders Gemini
content, has a working **native NSMenu bar** (`L4` app menu + File/Edit/View/
Bookmarks/Identity/Window/Help) with **native Cmd shortcuts**, and fetches pages
over ClassicNet. Key on-device fixes: the_Foundation big-endian archive bug
(`stream.c` `iHaveBigEndian`), the Finder `-psn_` argv, and the `[NSApp run]`
run-loop integration (see docs/arcana.md).

Evidence / reproduce (`scripts/build-osx.sh`, inside the amd64 `ubuntu:24.04`
container with the darwin8 toolchain):
- `L4.app/Contents/MacOS/L4` = `Mach-O ppc executable`. Sources:
  the widget kit (`src/ui/*`), `src/render/text_stb.c`, `src/ui/canvas/sdlcompat.c`
  (software framebuffer), `src/macos/aquaview.m` (NSWindow + NSView +
  `NSBitmapImageRep` blit + NSEvent→shim event translation), `src/macos/aquamain.c`.
- On-device evidence: `~/classic/petal/logs/l4-aqua-native-menu-2026-09-10.png`
  (merged `L4` menu bar + window), `l4-aqua-ondevice-2026-09-10.png`,
  `l4-aqua-runtime-2026-09-10.txt` (log), `lagrange-d8-archive-tfdn-2026-09-10.txt`
  (the_Foundation big-endian ZIP proof).
- **Two mandatory `the_Foundation` deps cross-built for darwin8** (arcana):
  `libunistring 1.4.2` (prior witness) and **PCRE2 10.47** (`deps/pcre2-darwin8`,
  GNU/GitHub sig-verified — regexp is required by `gmdocument.c`'s `iRegExp`).
  zlib (`iHaveZlib`) comes from libSystem via `osx/pkgconfig/zlib.pc`.
- osx plumbing: `project(..., C OBJC)` + `CMAKE_OBJC_FLAGS` (`-std=gnu99
  -fobjc-exceptions`); quote-include fix (CarbonCore's `resources.h` shadow);
  `execinfo` shim (`darwin8_sdk_shim/execinfo.h`); `NDEBUG` (host canvas is
  Release). Prerequisite (still the portability witness): the `the_Foundation`
  ClassicNet seam cross-build + on-device fetch, `d8_tls_smoke`:

  - On-device (petal, iMac G4 / OS X 10.4.11 PPC; host capsule `192.168.7.146:1966`):
    `/tmp/d8_tls_smoke 192.168.7.146 1966 /` → `[classicnet] TCP connect` +
    `TLS handshake OK (mbedTLS via cn_tls)` + `d8_tls_smoke OK: status '20
    text/gemini' body=216 certSubj='CN = localhost' isVerified=1`. Evidence:
    `~/classic/petal/logs/lagrange-d8-tfdn-smoke-2026-09-10.txt`.
  - `build-osx/d8_tls_smoke` = `Mach-O executable ppc`; darwin8 toolchain:
    `~/classic/darwin8-toolchain` (danupsher GCC 15.2 + cctools-port + 10.4u SDK),
    docker `ubuntu:24.04` amd64 under Rosetta
    (`~/.local/share/doc/darwin8-toolchain.md`).

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
      docs/arcana.md. **Native Cmd shortcuts work** via menu-first key-equivalent handling +
    a widget-kit fallback in the Aqua window; quitting is clean (`atexit`
    `deinit_Foundation` so the AppKit-exit host doesn't trip the Foundation
    assert). Evidence: `~/classic/petal/logs/l4-aqua-native-menu-2026-09-10.png`.
    **Remaining:** the glyph `〉` (U+3009, sidebar collapse arrow) missing from the
   bundled fontpack (`failed to find 00003009`); context-menu popups
   (`showPopupMenu_MacOS` needs an `NSEvent`, deferred); a real network fetch +
   TOFU screenshot (the app does fetch over ClassicNet — `[classicnet] TCP/TLS
   tilde.club` — but a tactile TOFU shot is still pending). The app's real trust
   gate + `gmrequest`/`gmcerts` integration come with the fetch/TOFU slice.
 2. **N3 tactile confirmation (from Phase 1, still pending)** — the visual check
    that `canvaswin` shows a fetched page + the TOFU trust/mismatch UI on
   QEMU/petal (fetch is proven at the seam + headless-render level).
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
