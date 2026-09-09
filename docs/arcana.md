# Arcana — weird stuff learned on machine (ported + fresh)

Convention (inherited from starscape): scripts encode *offsets*; this file
encodes *why*, plus the dead ends that led there. Anything conclusively
explained should move into the relevant script/CMake and be trimmed here.
Machine paths/VM detail lives in user-level config and `~/.local/share/doc/*`.

Entries marked **[starscape T-x / M-x]** were ported verbatim-ish from that
repo's `docs/retro68-arcana.md` / `docs/plan-cocoa.md` — they are facts
about the platform/toolchain, not about starscape's code. They will be
re-verified against this app when our first on-device build touches them.

## Architecture — escaping SDL (the seams)

skyjake's `src/platform/` is a per-OS abstraction (macos/win32/x11/ios/
android) but the whole app still **assumes SDL2** underneath: SDL owns the
window, renderer, and event loop, and the platform files are OS-specific
trimmings on top. To land on Mac OS 8/9 (no SDL) and Tiger/Leopard without
an SDL3-era app, we add **seams that escape SDL**, each a portable contract
with one implementation per target:

```
skyjake:   platform_abstraction (mac/linux/win) → assumes SDL2
ours:      canvas_seam   stub SDL headers ↔ real backend (sdlview / Aqua / Toolbox)
           native_menu   canvasmenu contract ↔ per-target impl (macos=AppKit, classic=Menu Manager)
           network_seam  the_Foundation iSocket/iTlsRequest ↦ CNTransport
                         (cn_ot OS8/9 · cn_darwin8 host+Tiger · cn_tls mbedTLS)
```

- **Canvas seam**: the widget kit compiles against stub SDL headers; the shim
  (`sdlcompat.c`) supplies a software framebuffer + event queue; a backend
  (`sdlview` on macOS, future Aqua/Toolbox) does real device I/O. This is the
  "always use the canvas shim" model — SDL is just one backend for it.
- **Native menu**: SDL2 has *no* native-menu API, so menus talk straight to
  the OS UI (AppKit `NSMenu` on mac, Menu Manager `InsertMenu`/
  `SetMenuItemText`/`SetMenuItemCmdKey`/`CheckItem` on Classic), not SDL.
- **Network**: lagrange drives every fetch through the_Foundation's
  `iTlsRequest`/`iSocket` (`gmrequest.c`, a `Stream` subclass + an Object),
  so the seam keeps `gmrequest.c`/`gmcerts` *untouched* and reimplements
  just those two classes over ClassicNet's `CNTransport` vtable
  (`poll`/`send`/`recv`/`close`). `socket.c` backend = a `Stream` whose I/O
  drives a transport (`cn_ot` Classic, `cn_darwin8` host/Tiger); `tlsrequest.c`
  backend wraps `cn_tls` (mbedTLS, `CN_TLS_FORCE_TLS12=1`) above it. A
  `LAGRANGE_CLASSICNET` switch picks it for the canvas build (stock `app`
  keeps OpenSSL). Honest replacement via a vtable, not SDL.

**Menu-specific decisions (why this shape):**

- The menu contract stays *portable* (`canvasmenu.{c,h}` declares the ops:
  `insertMenuItems_*`, `enableMenuItem_*`, `showPopupMenu_*`, `submenuRoot_*`,
  …). Each target implements those exact symbols. So a Classic backend just
  reimplements them over the Menu Manager — no renaming of the shared names
  to `_Native` is needed; they're the same `_MacOS`-family interface all
  Apple targets implement. (Only the *compile-time gate* gets unified onto a
  single "native menu in use" marker, decoupled from `iPlatformAppleDesktop`,
  so the shim build can use native menus without flipping every macOS
  platform behavior.)
- **Do not drag `macos.m` into the shim build as the host backend.** It is
  coupled to things that fight the shim: it swaps `NSApplication`'s delegate,
  installs `ScrollWheel`/`KeyDown` local event monitors (which would *eat*
  the scroll events at the source, regressing the sdlview wheel path),
  and touches real SDL window internals (`nsWindow_`,
  `SDL_GetWindowWMInfo`, metal-renderer hint) that the canvas stub SDL
  headers don't provide. So the canvas host gets a **clean menu-only AppKit
  rewrite** (`canvasmenu_impl_SDL.m`) that liberally reuses macos.m's menu
  logic but omits the delegate swap, event monitors, and SDL-window coupling.
  `macos.m` stays for the legacy direct-SDL `app` build during the transition;
  the two implementations are never linked into the same binary.
- Host verification is via `osascript`/System Events querying the running
  process's menu bar items, so the abstract circle is proven end-to-end
  without eyeballing.
- **Multi-window is a backend requirement, not a Phase 0 host one.** The
  canvas host displays a single shim framebuffer: `SDL_RenderPresent`
  hardcodes `presentHook_(0)` and `sdlview.c` has one real window, so any
  extra shim window (detached Preferences, `window.new`) is tracked but
  never rendered — it grabs focus while invisible ("looks frozen"). For the
  Phase 0 host we decline a second window and force detached dialogs to
  in-window sheets (`detachedPrefs=0` on `LAGRANGE_CANVAS`). But the future
  **Aqua/Toolbox backends each need a real per-`iWindow` implementation**:
  one OS window per shim `iWindow`, position/size from the app window rect,
  and event routing keyed by shim window **id** (not index — the shim's
  swap-remove reorders indices on close).

## Toolchain / build (Retro68, M-tier)

- **Toolchain provenance matters.** Our local Retro68 build is configured
  with Apple's Universal Interfaces 3.4.2 (workspace-external machine
  artifact, see user-level doc), which provides the raw OpenTransport
  headers and — critically — *self-packing* Toolbox headers. If a rebuild
  reinstalls the multiversal symlinks, re-apply the Apple-header swap;
  keep a configure-time offset probe in the build (see packing below).
- **Configure-time struct-offset probe is mandatory.** Under the wrong
  (multiversal) headers, the Toolbox `#pragma options align=mac68k` is
  silently ignored by Retro68's GCC: `EventRecord.message` lands at 4
  instead of 2, `GrafPort.portRect` at 20 instead of 16, and the UI
  half-misbehaves (updateEvt storms, garbage messages). Probe:
  `offsetof(EventRecord, message) == 2`, `sizeof(GrafPort) == 108`,
  FATAL_ERROR with a pointer here. Never patch around it in source; never
  `-fpack-struct=2`; no per-file pack wrappers (Apple headers self-pack). **[starscape 2026-08-05/06/28 saga]**
- **Timeless rule:** struct field access "because it compiled and drew
  something" is not proof — GrafPort partially worked because early fields
  shared offsets under both layouts. Verify layout with an offset probe. **[starscape]**
- **Retro68 CMake**: `project(X C CXX)` always (CACHED only C errors at
  generate); CONSOLE apps force `LINKER_LANGUAGE CXX` and trip bare `ar`
  — archive objects via `add_library(... STATIC)`, never stock `ar`. **[starscape]**
- **ld signal-11 on many-TU CONSOLE links** — nondeterministic. Build
  module objects into a STATIC archive and link the runner + archive so
  final `ld` sees one input object. Expect this with a many-file app like
  lagrange's core. **[starscape]**
- **Static-link everything.** Retro68's CFM/PEF shared-lib recipe works
  "barely documented" (Retro68 #97; MakeImport quirks) — ClassicNet's
  DESIGN.md calls static the reliable vehicle today.
- **OT/lib set**: app-level libs (`OpenTransportAppPPC OpenTransportLib
  OpenTptInternetLib`) are an app property, not toolchain. **[starscape]**

## Resource forks / containers

- **Container zoo**: `Name.APPL` = PEF data fork; `%Name.ad` = AppleDouble
  resource fork only; `Name.bin` = MacBinary for push-to-target;
  `.dsk` = dead end (no partition map under QEMU OS 9); `.xcoff/.pef`
  are intermediates and `.bin` is only fresh after a FULL build. **[starscape]**
- **Retro68 "+2 offset" quirk**: its AppleDouble writer uses a 26-byte
  header (entries at +26) and a 24-byte resource-fork map header. Naive
  parsers read zeros — in a zero-read, try +2 before assuming corruption. **[starscape]**
- Flat-fork SIZE payload sits at `0x104` (4-byte BE length prefix per
  resource after `0x100`); a bare `add_application` SIZE is 1.0/1.0 MB —
  set a real preferred size for a browser (lagrange will want several MB
  partitions; tune the SIZE resource accordingly). **[starscape]**
- Rez: `resource 'SIZE' -1` needs `#include "Types.r"`; the flag list is
  all-16-tokens-or-nothing; stock SIZE syntax only. **[starscape]**

## mbedTLS for PPC (cy384 fork / `deps/mbedtls-ppc`)

- It **yields to cooperative threads during long bignum work**
  (`YieldToAnyThread` in `bignum.c`) — link `ThreadsLib` or final link
  dies with undefined `.YieldToAnyThread`. And per our Thread-Manager
  architecture, that yield is what interleaves TLS slices with the event
  loop — never disable it. **[starscape]**
- **No sane `mbedtls_time()` on OS 9** — wire the Mac clock in at compile
  time via `MBEDTLS_USER_CONFIG_FILE` (`MBEDTLS_PLATFORM_TIME_MACRO`) +
  a `cn_mac_time`-style module, or EVERY cert fails expiry. ClassicNet's
  `target/cn_mac_time.c` is the template. **[starscape]**
- **Byte-array CAs are not length-bounded** — pass `len + 1` (trailing
  NUL) or every parse dies `-8576`/`MBEDTLS_ERR_PK_ALLOC_FAILED`, a
  misleading allocation-shaped error for a truncation bug. **[starscape]**
- Error taxonomy: `-0x6C00` = verify failed → read
  `mbedtls_ssl_get_verify_result()` flags for *why* (EXPIRED/FUTURE/NOT_CA
  bits); `-0x2180` = alloc failure, usually CA parse. **[starscape]**
- **`CN_TLS_FORCE_TLS12=1` everywhere.** Vanilla mbedTLS 3.6 over TLS 1.3
  gets `SSLV3_ALERT_BAD_CERTIFICATE` from python/openssl servers even in
  compatibility mode (T-1 on-device, reproducible from host builds).
  TLS 1.2 satisfies the gemini spec floor (1.2+). **[starscape T-1]**
- Host mbedTLS runs the same verify suite, so logic defects are PPC-fork-
  specific only if host passes + target fails with identical inputs. **[starscape]**

## Darwin8 / Tiger (T-tier)

- **`-Wl,-force_cpusubtype_ALL` on EVERY link line.** Cross-built deps can
  for-load ppc_970-subtype archive members (libjpeg-turbo's emutls was the
  precedent); classic ld max-combines and tags the *executable* ppc_970,
  which petal's G4 refuses (Bad CPU type, no crash log). `-mcpu=7400`
  compile flags do NOT fix it. Starscape bakes this into osx
  CMakeLists — do the same for lagrange flavors. **[starscape T-5]**
- **Configure-time 64-bit-division link probe** for the darwin8 flavor
  (toolchain-regression guard, the M-tier analog of the packing probe). **[starscape T-2]**
- Darwin 8 libc quirks: no `CLOCK_MONOTONIC`; `_POSIX_C_SOURCE` hides
  `gmtime_r`; `/dev/urandom` entropy works without hardening (mbedTLS 3.6
  entropy poll). **[starscape T-1/T-2]**
- **fd-collision class of bugs on Tiger resolver churn**: repeated
  `getaddrinfo`/socket-close cycles can close an unrelated low fd and
  alias later fds. ClassicNet's fix order (wake pipe AFTER getaddrinfo +
  socket) is upstream in `darwin8-transport` 8e0df7a — do not unlink it.
  Any fd-collision diagnostic starts from an fd-number trace at create
  time. **[starscape T-2]**
- **Do not probe ssl internals on Tiger builds** — `mbedtls_ssl_get_version`
  crashes there (root cause parked); read negotiated version server-side. **[starscape T-1]**
- Cross-compiler only runs inside the docker container (Ubuntu 24.04 base;
  glibc 2.38+ needed). Build dir + flag set live in a `osx/CMakeLists.txt`
  + wrapper-script pattern, docker-wrapped — not host-native. **[starscape T-2]**

## Tiger AppKit (T-tier UI)

Era-correct AppKit facts, expect all of these again when wiring
lagrange's canvas host into AppKit on 10.4: **[starscape T-3/T-4]**

- `NSInteger`/`NSUInteger` are 10.5+ typedefs on the 10.4u SDK — use
  `int`/`unsigned`.
- 10.4's `NSAlert` returns LEGACY panel codes (1/0/-1), not the 1000
  family — the header lies (1000-family constants match only 10.5+
  runtimes). Accept both families.
- 10.5-only APIs that bite in a day: `NSTrackingArea`,
  `characterIndexForPoint:`, `setAccessoryView:`,
  `NSFileManager createDirectoryAtPath:withIntermediateDirectories:`.
  Guard 10.5+ selectors with `respondsToSelector:`.
- AppKit silently swallows delegate/timer exceptions on Tiger (no crash
  log for Finder-launched apps). `@try/@catch` around UI assembly +
  `NSSetUncaughtExceptionHandler` + app-owned logfile (`Gemini.log`
  discipline: bare execs over ssh can't even reach WindowServer).
- Autoresize masks are edge-pinning, not flexing: bottom-pinned view =
  `NSViewMaxYMargin` (flex the TOP), and a missing width mask silently
  kills all resize reflow.
- Deploy: ship `.app` bundles via scp (Tiger's tar rejects gzipped
  bundles); `killall` old instances by name first (Tiger instance
  stacking); `screencapture` has no `-R` (full-screen + host-side crop).

## Classic (OS 9) runtime — QuickDraw / events / files

All **[starscape, on-device verified]**; expect to need every one when
lagrange's OS 9 canvas host lands (Phase M):

- **`BeginUpdate` needs the window's port CURRENT** — stale port means
  the update-region clip never applies and the next `EraseRect(portRect)`
  clears the window every event (white-flash storm). `SetPort(w)`
  immediately before `BeginUpdate(w)`; ALWAYS pair `EndUpdate`, even on
  early-exit/foreign-window paths — an unpaired Begin/End means OS 9
  re-queues updateEvt forever (18,701-line storm precedent).
- **Never disk I/O between erase and paint** — each `FSWrite`+`FlushVol`
  is tens of ms at HD latency and reads as a clear-then-draw flash. Log
  before BeginUpdate or after EndUpdate; debug logging behind a build
  option.
- **Edit-text control TE traps** (if the URL bar uses AM edit-text):
  the control's TE inherits the port font at creation — arm the port
  font before first focus; NEVER `TEUpdate` the field (CDEF paints at
  system size); caret/selection only render while TEActivated; blur must
  `TEDeactivate` → clamp selEnd (Cmd-A sets 32767) → `TESetSelect(off,off)`
  → `DrawOneControl` now → `InvalRect` (else "flash of old cursor").
- **`NewGWorld` needs a bounds rect** (`NULL` bounds → `paramErr -50`).
- **32bpp PixMap pack is XRGB** — first byte of each 4 unused; copying
  packed RGB888 into it shifts channels/"stripes". Expand `dst[4x]=0,
  [+1]=R, [+2]=G, [+3]=B`.
- **CopyBits depth conversion consults the port's FORE/BACK colors** —
  leftover link-blue FORE tints subsequently CopyBits'd images blue.
  Reset fore to black right before an image block's `CopyBits`.
- **Screen depth participates too** — verify suspected pixel bugs at
  Millions before hunting code; 16-bit direct conversion hue-shifts
  (RGB555) legitimately.
- **AA text has no per-pixel alpha in QuickDraw** — `CopyBits`/`CopyMask`
  can't src-over blend. The Toolbox canvas host composites glyphs with a
  software src-over pass over the GWorld buffer (`out=(src*sA+dst*(255-sA))/255`),
  the exact loop the host shim runs in `sdlcompat.c:1004-1010`. So the stb
  grayscale alpha-ramp glyph cache composites identically on Classic; no
  hinting/grid-fitting is needed because AA is correct at small ppem where
  1-bit breaks (the reason 1-bit + pixel-aligned fonts was parked).
- **`FSpOpenDF(fsWrPerm)` does NOT truncate** — `SetEOF(ref, 0)` after
  open, or follow-up log retrieves read a previous boot's stale tail
  (cost one imagined "phantom fetch" forensics round). `FlushVol(0, 0)`
  — the volume param is a short.
- `xxd -i` names the symbol from the full input path (slashes → `_`)
  and emits no NUL — run it on a bare basename for clean symbols.

## Loop / evidence (inherited discipline)

- Bundle diagnostics into ONE console app per QEMU boot: boot-to-evidence
  ≈ 75 s; one hypothesis per build would be the time sync of the project. **[starscape]**
- Headless output is invisible: the screenshot (or a mirrored log file)
  is the artifact; console apps via Startup Items need the "Press Return"
  pause unless the build drops it. **[starscape]**
- **petal's Classic (Blue Box) does not share Tiger's loopback** — 127.0.0.1
  inside Classic is the Classic stack's own stack; test servers for the
  OS 9 flavor must bind the LAN (the test-server `--bind` flag pattern) or
  QEMU's `10.0.2.2` gateway. Tiger-native apps DO reach ssh -R tunnels. **[starscape T-3]**
- PPM screenshots have one filter byte per scanline patterns only when
  converted wrong — raw PPM is ground truth; don't downscale before
  asking a vision model to read bitmap fonts. **[starscape]**

## Canvas shim ↔ real SDL2 parity (host, phase 0)

- **Blend `NONE` is a straight copy in SDL software render**: color mod
  applies, src alpha ignored for color, dst alpha *replaced*, alpha mod
  has no effect. Anything else (blending "by hand" under NONE) garbles
  the glyph-cache fill (bufTex→cache) and composites wrong forever
  after. Dst-alpha over-formula is linear: `sA + dA*(255-sA)/255` — a
  quadratic `sA*sA` variant compounds wrongly. The
  `src/ui/canvas/tests/shimtext.c` harness pins this bit-exactly vs
  real SDL2 at 1x and 2x.
- **`SDL_SetRenderTarget` resets the clip rect.** A stale clip silently
  clips away glyph-cache *writes* — glyphs then "draw" from empty cache
  cells: at 2x only stray fragments (an "i"-looking dash) survived.
  Looked like a rasterizer bug; it wasn't.
- **`SDL_RenderClear` must honor the draw-color alpha**: prerendered
  TextBufs clear to `(255,255,255,0)`; forcing the clear opaque painted
  white boxes behind every input-field glyph.
- **`SDL_TOUCH_MOUSEID` is `(Uint32)-1`, not 0.** Defining it as 0 made
  every real mouse match the touch-mouse check in `mouseCoord_Window`,
  which then returned `latestPosition_Touch()` = (0,0): sidebar list
  clicks un-hovered but never acted. Symptom signature: "hover works,
  click deselects hover, no action".
- **The app gates widget drawing on `isExposed_Window`** — a window
  that never receives `SDL_WINDOWEVENT_EXPOSED`/`ENTER` stays blank
  forever. Exposure is only *forced* in the state-restore path, so
  first-run + shim = blank canvas; `SDL_ShowWindow` must synthesize
  SHOWN/EXPOSED.
- **Two-level namespace bites dlopen+direct-link mixes**: canvaswin
  directly linked libSDL2 (for sdlview) *and* defined shim symbols in
  the same image; app calls recorded at link time bound to the real
  dylib, silently bypassing the shim (shim traces never fired, real
  `SDL_GetMouseState` returned (0,0) from the wrong window). Fix:
  dlopen-only, `RTLD_LOCAL`, never link the real SDL into a shim
  binary. Symptom signature: some shim traces fire, others never do.
- **`open --args` / bundle staleness**: `CanvasWin.app` is hand-assembled;
  a rebuild only refreshes `build-canvas/canvaswin` — the bundle keeps
  running the OLD binary (cost: a phantom 100% CPU "hang" that was just
  the busy-wait `SDL_Delay` + headless mode). `cp` the binary in and
  re-codesign after every rebuild. `open` may silently fail to launch;
  direct shell launch works.
- **Canvas builds must not read the real user config**: `~/.config/
  lagrange`'s saved window state restored a second (blank) window that
  stole event routing. Isolated state dir via `SDL_GetPrefPath`.
- **Instrumentation can be the crash**: a debug `fprintf` that
  dereferences `r->target` behind a `r->target ? "a" : "b"` ternary
  still evaluates `r->target->w` for the args — the ternary picks the
  string, not the deref. Crash signature: SIGSEGV at 0x0 inside
  SDL_RenderCopy only when the log env var is set.
- **lagrange's per-pixel wheel flag is a custom bit in `direction`,
  never set by real SDL2.** `isPerPixel_MouseWheelEvent` reads
  `ev.wheel.direction & iBit(9)` (`iBit(n) = 1U<<(n-1)`, so bit 8 =
  `1u<<8`), but stock SDL2 puts only `SDL_MOUSEWHEEL_NORMAL/FLIPPED`
  (0/1) there. The native macOS backend sets it itself in
  `src/platform/macos.m` (`setPerPixel_MouseWheelEvent`); the shim
  viewer (sdlview.c) must too, or every widget falls into the *notched*
  wheel path and multiplies each small trackpad delta by
  `3 * lineHeight`/`3 * itemHeight` — symptom is "scrolling works but
  way too fast". The patched SDL2 (`sdl2.26-macos-ios.diff`) signals
  precise scroll by leaving `which == 0` and forcing imprecise notched
  wheels to `which == 1`, so sdlview keys the flag off `which == 0`.
  It then must scale the point delta by the app pixel ratio
  (`CANVAS_SCALE`, `g_canvasScale`) to feed canvas-pixel scroll offsets.
  Inertia/scroll-finished (`iBit(10)/iBit(11)`) are *not* exposed by the
  SDL2 patch (no momentum phase), so they stay unset here.

## Phase 1 host wiring (N1) — ClassicNet on the host

- **Submodule pin is not starscape's.** starscape's `vendor/ClassicNet`
  sits one local-ahead commit (`57ca5db`, "cn_tls: optional client-cert
  identity in CN_TlsCreate") beyond the public `origin/darwin8-transport`
  tip; that commit was never pushed, so `git fetch origin darwin8-transport`
  cannot reach it. lagrange therefore pins the **origin tip `8e0df7a`**, which
  has a **6-arg `CN_TlsCreate(tls, inner, hostname, caPem, caLen, out)`**.
  The delta (client-cert identity → 10-arg `CN_TlsCreate`) is the Gemini auth
  model (a later milestone), so N1 does not need it. Bump the pin to the
  client-cert commit *and* switch to the 10-arg call when it lands.
- **Host mbedTLS 3.6 (`mbedtls-host3`) is a gitignored build artifact** under
  `vendor/ClassicNet/deps/`, provisioned by `scripts/setup-classicnet.sh`
  (`git clone --branch v3.6.0` + `make lib`). It is the vanilla host build of
  the same 3.6 line the PPC/darwin8 flavors use, so the host slice exercises
  the same wire behaviour. mbedTLS 3.6 `make lib` emits harmless
  `-Wunterminated-string-initialization` warnings in `ssl_tls13_keys.c`; the
  PPC build needed `MBEDTLS_FATAL_WARNINGS=Off` for the same thing.
- **classicnet's sanitizers are directory-scoped.** `add_compile_options`
  `-fsanitize=address,undefined` inside `vendor/ClassicNet` apply to that
  subdir's targets only. An executable defined in the *parent* (the lagrange
  smoke test, `gmclassicnet_smoke`) that links `libclassicnet.a` must add its
  own `-fsanitize=address,undefined` + link flags or the final link fails with
  undefined `__asan_*`. The `classicnet` PUBLIC compile definitions
  (`CN_HOST`, `CN_WITH_DARWIN8`, `CN_WITH_MBEDTLS`) and include dirs (incl.
  `MBEDTLS_ROOT/include`) *do* propagate — headers need only `link classicnet`.
- **`CN_TLS_FORCE_TLS12` is a compile definition you put on the classicnet
  target**, not a ClassicNet CMake option: `target_compile_definitions(classicnet
  PRIVATE CN_TLS_FORCE_TLS12=1)` after `add_subdirectory`. It makes cn_tls.c cap
  the max TLS version at 1.2 (the on-target-verified safe floor).
- **Driving `cn_darwin8` + `cn_tls` manually (the smoke's pump):** the darwin8
  connect needs a **POLLOUT (write)** wait — `CN_Darwin8Wait(&tcp, ms, 1)` —
  because `d8_poll` only reports connect completion on write readiness; the
  mbedTLS **handshake and body read drive off POLLIN (read-wait)** —
  `CN_Darwin8Wait(&tcp, ms, 0)` — because a freshly connected socket's send
  side rarely blocks, so the ClientHello flushes and the subsequent work is
  reads. Retry semantics: `mbedtls_ssl_write` on `WANT_READ/WANT_WRITE`
  requires the **same** app-data pointer/length, so in a send loop do **not**
  advance `sent` when the transport reports `got==0`; only advance past bytes
  actually accepted. A Gemini server sends `20 text/gemini\r\n<body>` then
  closes, so read until EOF (`recv` returning `eof`), find the **first CRLF**
  as the head terminator, treat the first two bytes as the status.

## Dead ends (proven — do not retry) **[starscape]**

- Secure Transport on Tiger/Classic: TLS 1.0 max — double dead for gemini.
- curl as gemini transport: no scheme support upstream, TOFU shoehorning.
- `.dsk` flat image as QEMU OS 9 boot volume: "Initialize" only.
- Bare exec over ssh reaching WindowServer on Tiger: dead end
  (`CFMessagePort bootstrap_register failed 1100`); Finder `.app` only.
- clang/LLVM for PPC32 Darwin: dropped upstream.
- `attempt_4`-style exotic Rez SIZE syntax: crashes Rez.
