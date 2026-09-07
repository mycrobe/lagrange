# Arcana — weird stuff learned on machine (ported + fresh)

Convention (inherited from starscape): scripts encode *offsets*; this file
encodes *why*, plus the dead ends that led there. Anything conclusively
explained should move into the relevant script/CMake and be trimmed here.
Machine paths/VM detail lives in user-level config and `~/.local/share/doc/*`.

Entries marked **[starscape T-x / M-x]** were ported verbatim-ish from that
repo's `docs/retro68-arcana.md` / `docs/plan-cocoa.md` — they are facts
about the platform/toolchain, not about starscape's code. They will be
re-verified against this app when our first on-device build touches them.

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

## Dead ends (proven — do not retry) **[starscape]**

- Secure Transport on Tiger/Classic: TLS 1.0 max — double dead for gemini.
- curl as gemini transport: no scheme support upstream, TOFU shoehorning.
- `.dsk` flat image as QEMU OS 9 boot volume: "Initialize" only.
- Bare exec over ssh reaching WindowServer on Tiger: dead end
  (`CFMessagePort bootstrap_register failed 1100`); Finder `.app` only.
- clang/LLVM for PPC32 Darwin: dropped upstream.
- `attempt_4`-style exotic Rez SIZE syntax: crashes Rez.
