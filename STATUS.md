# STATUS

## Where we are

**Phase 1 — ClassicNet network seam (host-first); N1 is done, N2 is next.**
Phase order is **1) ClassicNet on the host → 2) Tiger/Cocoa → 3) Classic**,
with the cross-build tool systems as parallel enabling work. The seam reuses
the "escape SDL" pattern for networking: keep `gmrequest.c`/`gmcerts` untouched
and reimplement the_Foundation's `iSocket`/`iTlsRequest` over ClassicNet's
`CNTransport` vtable (`cn_darwin8` host/Tiger, `cn_ot` Classic, `cn_tls`
mbedTLS, `CN_TLS_FORCE_TLS12=1`) behind the identical public API, with a
`LAGRANGE_CLASSICNET` compile-time switch.

**N1 (host wiring) is complete and gated on the host this session (2026-09-09):**
ClassicNet is vendored as a git submodule at `vendor/ClassicNet` (pinned to the
public `origin/darwin8-transport` tip `8e0df7a` — note: starscape's ClassicNet
is one local-ahead commit `57ca5db`, never pushed, that adds the 10-arg
client-cert `CN_TlsCreate`; lagrange pins the origin tip with the 6-arg call,
client-cert identity is a later milestone). Host mbedTLS 3.6 (`mbedtls-host3`)
is provisioned under `vendor/ClassicNet/deps/` by `scripts/setup-classicnet.sh`
(gitignored build artifact). The slice is wired into the lagrange build behind
`-DENABLE_CLASSICNET=ON` (`cmake/ClassicNet.cmake`), which builds the
`classicnet` host lib and a `gmclassicnet_smoke` host fetch. Gates all green:
ClassicNet's own host slice **13/13 (ASan)** in the vendored submodule, and a
**real Gemini fetch over `cn_darwin8` + `cn_tls`** returns `20 text/gemini` with
a non-empty body, verified against a test CA. Stock `app` and `canvasapp` still
build clean.

**Typography is parked** (was a host-testable Phase-1 item). Classic renders
**grayscale AA** (CopyBits has no per-pixel alpha → a software src-over
composite over the GWorld buffer; see arcana); the 1-bit "Platinum-sharp" UI +
pixel-aligned bitmap fontpack + per-spec `smooth` machinery are deferred
nice-to-haves.

Phase 0 (canvas seam) and the Ph0.5 native macOS menu bar are complete; the
seam is build-level (stock `app` unchanged, canvasapp/canvaswin on the shim),
with menus keyed off the single `LAGRANGE_NATIVE_MENU` marker. The Phase-0
viewer defaults to 1x rasterization (`CANVAS_SCALE=1`) validated on this 2x
display.

**Runtime environment (this machine, user-level)**: homebrew `sdl2` alias =
sdl2-compat (broken Retina coordinates). Stock/canvas builds use **real SDL2
2.26.5** patched with `sdl2.26-macos-ios.diff`, built to `/tmp/kilo/sdl2`
(VOLATILE). Kill every Lagrange/canvasapp before launching a build (IPC steal
rule, AGENTS.md). Canvas builds use an isolated state dir
(`/tmp/kilo/canvas-home`, `CANVAS_PREF_DIR`).

## Last completed milestone

**N1 — ClassicNet host wiring** (this session, 2026-09-09). Vendored
`vendor/ClassicNet` as a git submodule pinned to `8e0df7a`
(`origin/darwin8-transport`), provisioned host mbedTLS 3.6 into
`vendor/ClassicNet/deps/mbedtls-host3` via `scripts/setup-classicnet.sh`, and
wired the host slice into the lagrange build behind `-DENABLE_CLASSICNET=ON`
(`cmake/ClassicNet.cmake` + option in `CMakeLists.txt`). Added a real Gemini
host smoke (`tests/classicnet/gmclassicnet_smoke.c`) that drives
`cn_darwin8`+`cn_tls` directly through the `CNTransport` vtable and asserts a
2x status + non-empty body, plus a local TLS Gemini server
(`tests/classicnet/gemini_tls_server.py`) and the gate runner
(`scripts/test-classicnet-n1.sh`).

Evidence / reproduce:
- ClassicNet 13/13 (ASan), vendored submodule:
  `scripts/setup-classicnet.sh` then, inside `vendor/ClassicNet`,
  `cmake -S . -B build-host-tls3 -DCN_WITH_MBEDTLS=ON -DMBEDTLS_ROOT=deps/mbedtls-host3 && cmake --build build-host-tls3 -j8 && ctest --test-dir build-host-tls3` → `100% tests passed out of 13`.
- Smoke fetch, verified:
  `cmake -S . -B build-classicnet -DENABLE_CLASSICNET=ON -DENABLE_GUI=OFF -DENABLE_HARFBUZZ=OFF -DENABLE_FRIBIDI=OFF` then
  `cmake --build build-classicnet --target gmclassicnet_smoke -j8` then
  `scripts/test-classicnet-n1.sh ./build-classicnet/gmclassicnet_smoke`
  → `OK: ClassicNet host slice 13/13 (ASan)` + `OK: Gemini status '20' head '20 text/gemini' -> 66 body bytes` + `OK: Gemini smoke fetch -> 2x status + non-empty body`.
- One-shot via ctest: `ctest --test-dir build-classicnet --output-on-failure`
  → `Test #1: classicnet_n1 ... Passed`.
- Regression gate: `cmake --build build-host --target app -j8` and
  `cmake --build build-canvas --target canvasapp -j8` both stay green.

## In-flight

1. **N2 — the seam** (next). Implement the `Socket`/`TlsRequest` backends over
   `CNTransport` in `the_Foundation`: a `Stream` subclass whose I/O drives a
   `CNTransport` and fires the `connected`/`readyRead`/`error`/`disconnected`
   audiences; a `TlsRequest` backend wrapping `cn_tls` (mbedTLS,
   `CN_TLS_FORCE_TLS12=1`) mapping `submit`/`readAll`/`serverCertificate`/
   `isVerified`/`setVerifyFunc` to mbedTLS + `gmcerts`. Branch inside the
   `the_Foundation` submodule (named branch, host-tested, pin bumped in the
   consuming commit). **Gate:** host unit tests of both backends against
   ClassicNet's loopback `CNTransport` (framing + audiences + cert-verify →
   `gmcerts`), ASan/UBSan clean; `build-host` stock `app` still green.
2. **N3 — into the canvas app.** Wire the seam into `canvaswin` so the viewer
   actually fetches a Gemini page over ClassicNet. Gate: integration test
   fetches a real Gemini URL asserting status/meta/body + the TOFU
   pin/mismatch gate; `canvaswin` shows the page.
3. ClassicNet submodule pin: lagrange is at `8e0df7a` (6-arg `CN_TlsCreate`).
   When the client-cert identity commit (`57ca5db`) lands on `origin`, bump the
   pin and migrate the call to the 10-arg form as part of the Gemini auth work.
