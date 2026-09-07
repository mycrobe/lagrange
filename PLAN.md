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

### Phase 1 — Build systems

- **Retro68** (`RETRO68_TOOLCHAIN`, machine artifact; C11 verified on both
  `powerpc-apple-macos-gcc` and `m68k-apple-macos-gcc` targets). Static
  link everything (the Retro68 CFM-shlib bug makes static the reliable
  vehicle; mirrors Starscape).
- **darwin8 cross toolchain** (Starscape's GCC 15.2 + cc1obj + cctools +
  10.4u SDK pipeline, docker-wrapped, CMake-driven via `scripts/build-osx.sh`
  pattern). Carry over: `-Wl,-force_cpusubtype_ALL` on every link line
  (ppc_970 emutls trap), configure-time 64-bit-division link probe.
- Cross-build vendored deps per tier: mbedTLS (`mbedtls-darwin8` from
  Starscape; an `mbedtls-ppc` Retro68 build), freetype unnecessary (STB
  text rendering is self-contained; keep `text_stb`).
- `the_Foundation` darwin8 + Retro68 builds: expect to shim/replace
  POSIX-ish bits (threads → TM shim on M-tier; native pthread on
  darwin8), atomics, time, paths, sockets.

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
- **Menu shim**: port `src/platform/macos.m`'s NSMenu bridge to the Menu
  Manager (`InsertMenu`/`SetMenuItemText`/`SetMenuItemCmdKey`/`CheckItem`),
  rebuilt at the same points, posting back into `postCommand_App`. Use
  `MenuHook`/`TESetIdleHook` for `CN_Idle` keep-alives during menu tours.
- Fonts: keep the STB/lagrange fontpack stack — no FOND/FONT resources,
  no WorldScript/TEC layer (UTF-8 preserved).
- Audio: Sound Manager double-buffer backend, or compiled out v1.
- Memory budgets: verify against a low-RAM G3 profile in QEMU early; the
  widget kit's bands are above Starscape's (~50K ctx / 256K buffers).

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
- [ ] the_Foundation on darwin8: expecting near-native (poll/pthread exist
      on Tiger); verify `-lresolv`/64-bit-division quirks from Starscape
      don't recur.
- [ ] App-layer feature trim for M-tier: which widgets to freeze (e.g. no
      media streaming, no misfin, no GPub, single-window) — decide per
      memory/simplicity budget before Phase 2.
- [ ] Upstream-consumable split: keep backend seams shaped so skylake
      could upstream the ClassicNet+mbedTLS backend independently.
