# Plan — lagrange for classic Mac OS (OS 8/9 PPC) and Tiger/Leopard OS X (PPC)

*2026-09-07. Planning/analysis phase; nothing machine-verified yet. Derived
from architecture analysis of this tree plus the Starscape T/M track
experience (OS 9 + 10.4 flavors of a Gemini client sharing one portable
core).*

## Goal

Port lagrange (a Gemini client, ~45K lines of SDL-based C UI + portable
core) to:

- **M-tier**: PowerPC Mac OS 8.0–9.2.2 (Retro68 toolchain, Toolbox UI over
  QuickDraw, Open Transport networking).
- **T-tier**: Mac OS X 10.4.11 PPC (Tiger) and 10.5 PPC (Leopard), native
  Aqua AppKit UI.
- (**Stretch, parked**: 10.3 Panther PPC — needs an unproven darwin7 cross
  toolchain; 68030/System 7 — different TLS and UI, no OT, parked like
  Starscape's scope guard.)
- 10.6–10.8 Intel falls out of the T-tier work for free if the Aqua backend
  is used there instead of stock SDL2 — no SDL3-era apps in scope; 10.9+
  already runs upstream lagrange.

## Strategy

Reusing the proven Starscape T/M split:

1. **One portable core, thin per-OS UI layers.** lagrange's widget kit
   (`src/ui/`, ~45K lines) is self-contained; SDL appears in narrow host
   roles (window/events ~9 call sites, renderer blits ~20, audio, OpenURL,
   clipboard, dialogs). The engine keeps its immediate-mode widget design;
   we do NOT rewrite it as native widgets. Each OS gets a "canvas host":
   the existing software renderer draws into a window-owned surface.

2. **One network seam for all targets**: ClassicNet (vendored from
   starscape) supplies TLS 1.2 (pinned) / mbedTLS 3.x over a `CNTransport`
   vtable that already has two transports — `cn_ot.c` (Open Transport, OS
   8/9) and `cn_darwin8.c` (BSD sockets, Tiger). This replaces
   `the_Foundation`'s OpenSSL-backed `tlsrequest.c` with a mbedTLS-backed
   backend behind the same `iTlsRequest`/`iSocket` API, so `gmcerts` and
   the app layer above are untouched.

3. **Thread Manager (cooperative) as the the_Foundation threading shim**
   (not MP Services): cooperative `NewThread`/`YieldToAnyThread` workers,
   no mutex contention (serialized lane, no-locks invariant like
   Starscape). MP Services stays a documented runtime upgrade path
   (preemptive TLS/layout worker on 9.1+ via `MPCreateTask`, fed with
   `MPCreateQueue`/kernel notifications), never a build dependency. Main
   loop drives OT cooperatively via `CN_Idle`; no OT calls from any
   preemptive task.

## Phases

### Phase 0 — Host canvas seam (do first; the only genuinely new risk)

Refactor the SDL host layer behind a slim interface so SDL becomes one
backend:

- Events in: translate platform events to the widget kit's input structs
  (currently SDL_Event-shaped, `src/ui/window.c` field).
- Surface out: window-attached framebuffer the `Paint` pipeline targets.
- Services: menus, audio device, clipboard, OpenURL, dialogs.

Three consumers: sdl (stock platforms, unchanged), aqua (T-tier), classic
(M-tier). Deliverable: headless build with a null backend + host unit
harness, stock SDL build still green.

**Phase ordering (2026-09-09).** The remaining work is now ordered:
**1) ClassicNet network seam on the host** (below) → **2) Tiger/Cocoa** →
**3) Classic**. Cross-build tool systems are the enabling work that runs in
*parallel* (only the host mbedTLS + ClassicNet host slice are needed for
step 1, not the cross compilers). This was re-sequenced so networking is
host-verified end-to-end before any real UI tier is built.

### Phase 1 — ClassicNet network seam (do next; host-first)

Reuse the proven "canvas seam" pattern for networking: build the Gemini
transport against ClassicNet on the **host first**, then the same slice
moves to the real tiers. Host-verifiable end to end; `build-host` stays
green.

**Why this shape.** lagrange drives every fetch through the_Foundation's
`iTlsRequest`/`iSocket` (`gmrequest.c` creates one, sets host/content,
submits, and reads `serverCertificate`/`isVerified`). ClassicNet is a
lower-level C callback API: a `CNTransport` vtable
(`poll`/`send`/`recv`/`close`) with `cn_darwin8.c` (BSD sockets — also the
host transport), `cn_ot.c` (Open Transport, Classic-only), and `cn_tls.c`
(mbedTLS layered above a transport). So the seam keeps `gmrequest.c`,
`gmcerts`, and everything above **untouched** and reimplements just the two
the_Foundation classes over `CNTransport` — "escaping SDL" again, now
"escaping the_Foundation's OpenSSL":

- `socket.c` backend — a `Stream` subclass whose I/O drives a
  `CNTransport` (`cn_darwin8` on host/Tiger, `cn_ot` on Classic), pushing
  bytes into the Stream buffer and firing the `connected`/`readyRead`/
  `error`/`disconnected` audiences.
- `tlsrequest.c` backend — wraps `cn_tls` (mbedTLS, TLS 1.2 pinned via
  `CN_TLS_FORCE_TLS12=1`) over that transport; maps `submit`/`readAll`/
  `serverCertificate`/`isVerified`/`setVerifyFunc` to mbedTLS + `gmcerts`.
- A compile-time switch (`LAGRANGE_CLASSICNET`) selects the ClassicNet
  backend for the canvas build; stock `app` keeps OpenSSL.

Milestones (host-verifiable; **each gates on its test before the next**):
- **N1 — host wiring.** ✅ **DONE (2026-09-09).** Vendor ClassicNet (host
  slice) + host mbedTLS (Starscape's `mbedtls-host3` tree) into the lagrange
  build; a host smoke test does a real Gemini/HTTPS fetch over `cn_darwin8` +
  `cn_tls`. *Evidence:* ClassicNet's own host build passes 13/13 ASan tests
  on this Mac (incl. `test_darwin8` — the transport — and `test_h2_download` —
  real I/O) and the real Gemini smoke fetch returns a `20 text/gemini` head
  (2x status) with a non-empty body, verified against a test CA.
  **Test gate:** ClassicNet host slice tests stay 13/13 (ASan) + the
  smoke-fetch asserts a non-empty body + 2xx status. See STATUS.md. Note the
  submodule pin: starscape's ClassicNet is one local-ahead (unpushed) commit
  (`57ca5db`, 10-arg client-cert `CN_TlsCreate`); lagrange pins the public
  `origin/darwin8-transport` tip `8e0df7a` (6-arg `CN_TlsCreate`) — client-cert
  identity is the Gemini auth milestone.
- **N2 — the seam (in progress).** Implement the `Socket`/`TlsRequest`
  backends over `CNTransport` (the bulk of the code; iteration-heavy).
  *Step 1 DONE (2026-09-09):* the `Socket` backend — a `Stream` subclass over
  a `CNTransport` (`cn_darwin8` host/Tiger, `cn_ot` Classic) firing
  `connected`/`readyRead`/`error`/`disconnected`/`bytesWritten`/`writeFinished`,
  selected by `TFDN_CLASSICNET=ON` in the `the_Foundation` submodule
  (`classicnet-seam` branch, pin `f30fdd8`). lagrange wires the flag + links
  `the_Foundation` to `classicnet`; host unit test against a loopback TCP echo
  server is ASan-clean. *Step 2 remains:* the `TlsRequest` backend over `cn_tls`
  (mbedTLS) + the `iTlsCertificate` X.509 wrapper → `gmcerts`.
  *Step-2 is monolithic:* `tlsrequest.c` defines both `iTlsCertificate` and
  `iTlsRequest`, and `gmcerts` uses nearly every cert method, so a partial port
  breaks linking; and `newSelfSignedRSA_TlsCertificate` (self-signed generation)
  is OpenSSL-only — mbedTLS has no cert generation. Land (2a) the transport +
  iTlsCertificate core (parse/verify/expiry/pem/fingerprint/domain) first, then
  (2b) name components + the self-signed workaround. ⚠️ push `the_Foundation`'s
  `classicnet-seam` branch (local-only) so the pin `f30fdd8` is reproducible.
  **Test gate:** host unit tests of the backends against ClassicNet's loopback
  `CNTransport` (framing + audiences + cert-verify → `gmcerts`), ASan/UBSan clean;
  `build-host` stock `app` still green.
- **N3 — into the canvas app.** Wire the seam into `canvaswin` so the
  viewer actually fetches a Gemini page over ClassicNet.
  **Test gate:** integration test fetches a real Gemini URL, asserting
  status/meta/body + the TOFU pin/mismatch gate; `canvaswin` shows the
  page.

### Phase 2 — Build systems (enabling toolchain, parallel)

- **Retro68** (`RETRO68_TOOLCHAIN`, machine artifact; C11 verified on both
  `powerpc-apple-macos-gcc` and `m68k-apple-macos-gcc` targets). Static
  link everything (the Retro68 CFM-shlib bug makes static the reliable
  vehicle; mirrors Starscape).
- **darwin8 cross toolchain** (Starscape's GCC 15.2 + cc1obj + cctools +
  10.4u SDK pipeline, docker-wrapped, CMake-driven via `scripts/build-osx.sh`
  pattern). Carry over: `-Wl,-force_cpusubtype_ALL` on every link line
  (ppc_970 emutls trap), configure-time 64-bit-division link probe.
  ✅ **STANDING (2026-09-09)**: `osx/darwin8.toolchain.cmake` +
  `osx/CMakeLists.txt` + `scripts/build-osx.sh` build the ClassicNet
  darwin8 slice (`cn_d8`) + mbedTLS-d8 into `build-osx/d8_smoke` (a PPC
  Mach-O), verified on petal (Tiger 10.4.11) with a real Gemini fetch over
  `cn_darwin8` + `cn_tls`. See STATUS.md. Next: cross-build `the_Foundation`
  (+ the seam) and the Aqua canvas host. ✅ **the_Foundation + seam DONE
  (2026-09-10)**: `the_Foundation` (static, `TFDN_CLASSICNET=ON`) cross-compiles
  for Tiger and `osx/d8_tls_smoke` fetches on petal via `iTlsRequest`; see
  STATUS.md. Onward: Aqua canvas host.
- Cross-build vendored deps per tier: mbedTLS (`mbedtls-darwin8` from
  Starscape; an `mbedtls-ppc` Retro68 build), **libunistring** (`deps/libunistring-darwin8`,
  a mandatory the_Foundation dep the original list missed), freetype
  unnecessary (STB text rendering is self-contained; keep `text_stb`).
- `the_Foundation` darwin8 + Retro68 builds: expect to shim/replace
  POSIX-ish bits (threads → TM shim on M-tier; native pthread on
  darwin8), atomics, time, paths, sockets.
- ~~Typography groundwork~~ **PARKED (nice-to-have, 2026-09-09)** — the
  per-spec `smooth` attribute + glyph-cache-key change in
  `src/render/text_stb.c`/`src/fontpack.c` and the pixel-aligned
  bitmap-source TTF fontpack only feed a 1-bit "Platinum-sharp" look.
  With AA-on-Classic as the primary path, this is not a Phase-1
  prerequisite; revisit only if the 1-bit idea returns. See the
  Typography section.

### Phase 2 — ClassicNet networking (MD- and T-tier shared backend)

- New `the_Foundation` TLS/socket backend over ClassicNet's `CNRequest`
  + `CN_TLS_FORCE_TLS12=1` (TLS 1.3 alert bug with TLS 1.2 spec floor;
  Starscape T-1 lesson).
- M-tier: `cn_ot.c` pump from `WaitNextEvent` (`CN_Idle`); T-tier:
  `cn_darwin8.c` wait/cancel loops (T-2 fd-churn fix already upstream in
  ClassicNet's `darwin8-transport`).
- Subclass the Starscape workflow: branch ClassicNet in a submodule,
  host-test under ASan/UBSan before cross-compiling, named pins bumped in
  the same commit that consumes them.
- Gemini specifics over the CN API: redirect loop (≤5 hops, self-redirect
  stop), TOFU pin/mismatch gate, 1x input mapping, MIME routing — all
  already shaped by Starscape's portable core; lagrange's equivalents sit
  in `gmcerts`/`gmrequest`, which stay on the new backend unchanged.

### Phase 2 — Classic (OS 9) UI

Per earlier analysis (paradigms more similar than the T-tier):

- Offscreen `GWorld`/`NewGWorld` framebuffer; blit via `CopyBits` to the
  `WindowPtr` port (scale + depth conversion at draw). 8-bit quantize is
  a display decision, never a worker decision (Starscape T-6 rule).
- `WaitNextEvent`/`EventRecord` → widget input translation.
- **Menu shim**: DON'T port `macos.m`'s AppKit code into Classic. Instead
  keep a portable menu *contract* (`canvasmenu.{c,h}` — the `_MacOS`-family
  menu ops) that every target implements: macOS/AppKit (`macos.m`, plus a
  clean menu-only `canvasmenu_impl_SDL.m` for the canvas host), Classic =
  Menu Manager (`InsertMenu`/`SetMenuItemText`/`SetMenuItemCmdKey`/
  `CheckItem`), rebuilt at the same points, posting back into
  `postCommand_App`. Use `MenuHook`/`TESetIdleHook` for `CN_Idle`
  keep-alives during menu tours. The menu gate is a single
  `LAGRANGE_NATIVE_MENU` marker, decoupled from `iPlatformAppleDesktop`,
  so the shim build can exercise native menus without flipping macOS
  platform behavior. (Model + gotchas: `docs/arcana.md` →
  "Architecture — escaping SDL".)
- Fonts: keep the STB/lagrange fontpack stack — no FOND/FONT resources,
  no WorldScript/TEC layer (UTF-8 preserved). See "Typography" below for
  the rendering decisions that follow from this.
- Audio: Sound Manager double-buffer backend, or compiled out v1.
- Memory budgets: verify against a low-RAM G3 profile in QEMU early; the
  widget kit's bands are above Starscape's (~50K ctx / 256K buffers).

### Typography — font rendering decisions (informs Phases 2/3)

Analysis of the current stack (`src/fontpack.c`, `src/render/text_stb.c`):
load TTF → FriBidi reorder → HarfBuzz shape → stb_truetype rasterize
(8-bit grayscale, unhinted) → RGBA4444 glyph atlas → `SDL_RenderCopy`
quads. All pieces except SDL are optional already: HarfBuzz and FriBidi
are compile-out with a working `runSimple_Font_` fallback; stb_truetype is
self-contained C. Everything runs fine against a software framebuffer
(the `ENABLE_CANVAS`/`sdlcompat.c` seam proves it on the host). No
FreeType/SDL_ttf anywhere.

**DEFERRED (nice-to-have, parked 2026-09-09):** the fine-grained per-spec
`smooth` machinery below existed to feed a 1-bit "Platinum-sharp" UI. With
AA-on-Classic as the primary path (the default alpha-ramp rendering needs
none of it), this is **not a Phase-1 prerequisite** — it's the mechanism to
revisit only if the 1-bit look is wanted later. Kept below for reference.

Key structural fact: smoothing is decided at *rasterization time per
glyph* via the palette chosen in `glyphPalette_()` (`text_stb.c:542`) —
`grayscale` (alpha ramp) vs `blackAndWhite` (1-bit, alpha≤100 threshold).
Both palette variants can coexist in one atlas and blend correctly at
draw time. So per-context smoothing is a small change:

- **Per-font smoothing attribute** instead of the global
  `prefs.fontSmoothing`: new `smooth` key per font spec in
  `res/fontpack.ini` (same knob pattern as `glyphscale`/`voffset`).
  `glyphPalette_()` consults the `iFont` being rasterized. Two call
  sites to thread the font through (lines 554, 783).
- **Cache-key bit**: fold the smooth flag into the glyph cache key so a
  font used at identical size in both UI and document contexts doesn't
  get a glyph rasterized under one palette reused under the other.
- Runtime toggling machinery (`app.c:4429` cache-flush path) becomes
  unnecessary — smoothing is fixed per spec at font-load time.

Tier policy:

- **Classic (M-tier)**: grayscale AA everywhere — the same stb alpha-ramp
  path the host/app already uses (the default `prefs.fontSmoothing`).
  Unhinted stb AA needs no grid-fitting, so small sizes render correctly
  (soft, not broken): the 1-bit "broken stems / filled bowls" problem
  doesn't apply to AA. Compositing: `CopyBits` has no per-pixel alpha, so
  the Toolbox canvas host draws glyphs with a software src-over pass over
  the GWorld buffer — the exact loop the shim already implements
  (`sdlcompat.c:1004-1010`; see arcana "AA compositing on Classic").
  Gamma/tonality: `text_stb.c:467` TODO — tune palette values after
  visual runs.
- **Aqua (T-tier)**: full grayscale AA everywhere (the normal lagrange
  look); `NSBitmapImageRep` handles alpha natively, so no extra
  compositing pass beyond what the canvas host already does.
- **Deferred 1-bit** (parked): a period-correct "Platinum-sharp" look would
  need pixel-aligned outline TTFs from bitmap sources (BDF/FON→TTF,
  e.g. px437/unscii) — per-spec `smooth=off` in a small built-in fontpack.
  Aesthetic only, not a correctness need; revisit on real hardware if the
  AA look is judged wrong. Oversampling/majority-threshold rasterization
  is the fallback for any ordinary outline UI font used in that mode.

Risks tracked: HarfBuzz 2.8.2 on Retro68 is the heavy C++ build on the
M-tier (FriBidi is small pure C — trivial); glyph-atlas memory + the CPU
of the per-pixel AA composite on the low-RAM G3 profile, measured in
Phase 2's QEMU budget check. Parked: the 1-bit Platinum look (aesthetic
only — correctness is covered by AA).

### Phase 3 — Aqua (10.4/10.5 PPC) UI

- 100% programmatic AppKit (no nibs — Tiger IB is GUI-only/binary;
  keeps everything host-buildable): programmatic `NSMenu` (the existing
  `src/platform/macos.m` bridge survives and is the primary), one window,
  the widget kit's canvas inside an `NSView` (`NSBitmapImageRep` on a
  `CALayer`/locked-focus draw), no NSTextView rewrite — Starscape's
  NSTextView lesson list largely doesn't apply to lagrange's own text
  stack. Manual retain/release, 10.4-era ObjC (no blocks/properties).
- Starscape T-3/T-4 gotcha list is reusable: `respondsToSelector:`
  guards for 10.5+ selectors, `@try/@catch` around UI assembly, app-owned
  log (Finder-launched apps don't inherit ssh stdout), `.app` bundles
  shipped via scp (Tiger's tar chokes on gzipped bundles), bare-exec can't
  reach WindowServer.
- Worker concurrency: one pthread + cmd/result pipes (T-tier has real
  pthreads; keep the serialized no-locks invariant, mapped to the
  Thread-Manager-shim shape where APIs converge).

### Phase 4 — 10.5 + decay tiers

- 10.5 PPC = deployment-target/SDK bump of Phase 3 (10.4-noted "10.5-only
  APIs" become free). Verify mbedTLS/cn_darwin8 unchanged.
- 10.6–10.8 Intel: same Aqua backend compiled by modern Xcode — or keep
  SDL2 there (both work; prefer the shared backend for one code path).

## Verification loop (inherited machine discipline)

Host-side portable tests (ASan/UBSan) → QEMU guest (OS 9; scripted,
repeatable) → real hardware (petal, iMac G4 10.4.11). Evidence captures
per Starscape's pattern; machine-specific evidence never enters the repo.

**Tests as you go (2026-09-09, explicit).** Every milestone lands with its
host test before the next starts; the stock `app` build stays green as the
regression gate. Unit and integration tests are written alongside the code
they cover — especially the networking stack (host machine):

- **Unit tests** (host, ASan/UBSan, no network): the new
  `Socket`/`TlsRequest` backends are driven against **ClassicNet's
  loopback/fake `CNTransport`** (the design doc's own host-test seam), so
  byte-stream/TLS framing + the `connected`/`readyRead`/`error`/`finished`
  audiences — and the cert-verify mapping to `gmcerts` — are exercised
  without the network. A the_Foundation-side CTest target in `build-host`.
- **Integration tests** (host, real network): an actual Gemini/HTTPS fetch
  over `cn_darwin8` + `cn_tls` against a real server, asserting
  status/meta/body and the TOFU certificate pin/mismatch gates. Run in the
  same pass as the unit tests.
- ClassicNet's *own* host slice tests are the transport/protocol layer's
  regression gate and must keep passing (currently 13/13 on this Mac).

## Version support matrix ("no-SDL3" era)

| OS | Tier | Toolchain | UI | Net |
|----|------|-----------|-----|-----|
| 8.0–9.2.2 PPC | M | Retro68 | QuickDraw/GWorld + Menu Manager | cn_ot + mbedTLS-ppc |
| 10.4–10.5 PPC | T | GCC 15 darwin8/9 cross (10.4u SDK) | Aqua AppKit canvas host | cn_darwin8 + mbedTLS-d8 |
| 10.6–10.8 Intel | T (free) | Xcode or same cross | Aqua host (shared backend) | same |
| 10.3 Panther PPC | parked | darwin7 cross (unproven) | — | — |
| 68030/System 7 | parked | Retro68-68k | — | no OT/Mac-TLS |
| 10.9+ | n/a | any | upstream (SDL2/SDL3 era) | upstream |

## Open questions

- [ ] the_Foundation on Retro68: which modules compile as-is vs shim?
      (socket/request/tls get replaced by ClassicNet; threadpool, eventloop,
      addressinfo need review.)
- [x] the_Foundation on darwin8: answered (2026-09-10) — near-native EXCEPT
      four gaps: mandatory `libunistring` (cross-build, gnulib
      `AVOID_ANY_THREADS` config fix), `strnlen`/`clock_gettime`/
      `pthread_setname_np` (shim), and `<spawn.h>`/`posix_spawn` (shim).
      `-lresolv` + the 64-bit-division quirks did NOT recur. See
      docs/arcana.md "the_Foundation on darwin8" + STATUS.md.
- [ ] App-layer feature trim for M-tier: which widgets to freeze (e.g. no
      media streaming, no misfin, no GPub, single-window) — decide per
      memory/simplicity budget before Phase 2.
- [ ] Upstream-consumable split: keep backend seams shaped so skylake
      could upstream the ClassicNet+mbedTLS backend independently.

## Licensing — classicnet seam (compatibility note)

The ClassicNet network seam mixes two permissive licenses; they combine
cleanly (no copyleft/taint), but the combined binary is *component-licensed*
and redistribution must keep each license intact.

| Component | License | Copyright |
|-----------|---------|-----------|
| lagrange | BSD-2-Clause | © Jaakko Keränen |
| the_Foundation | BSD-2-Clause | © Jaakko Keränen |
| ClassicNet (`vendor/ClassicNet`) | **Apache-2.0** | © Yung-Luen Lan |
| Mbed TLS (build-time dep, not vendored here) | Apache-2.0 | Mbed-TLS |

**Verdict:** Apache-2.0 (ClassicNet) is compatible with BSD-2-Clause
(lagrange/the_Foundation): both are permissive, ClassicNet can be statically
linked into a BSD-2-Clause app and the result distributed. They do not get
"absorbed" — the Apache-2.0 terms continue to apply to the ClassicNet
portion.

**Redistribution obligations (the only real constraint):**
1. Keep ClassicNet's Apache-2.0 `LICENSE`, `NOTICE`, and copyright headers
   intact (the `NOTICE` also carries its Mbed TLS / Retro68 / RFC 7541
   dependency + HPACK attribution).
2. Include the Apache-2.0 §4(B) notice text for the ClassicNet portion.
3. Keep the BSD-2-Clause notices for lagrange/the_Foundation — no relicensing
   of those components is required or allowed.
4. ClassicNet's Apache-2.0 patent grant applies to its own portion.

**Housekeeping:** lagrange's `the_Foundation` fork is under the same
BSD-2-Clause as upstream (the seam commits add code under that license); do not
add an Apache-2.0 header to lagrange/the_Foundation sources. Keep ClassicNet as
a clearly separated vendored dependency (`vendor/ClassicNet`), as it already is.
