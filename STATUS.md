# STATUS

## Where we are

**Phase 1 — ClassicNet network seam (host-first); N1 done, N2 step 1 (Socket)
done, N2 step 2 (TlsRequest) next.**
Phase order is **1) ClassicNet on the host → 2) Tiger/Cocoa → 3) Classic**,
with the cross-build tool systems as parallel enabling work. The seam reuses
the "escape SDL" pattern for networking: keep `gmrequest.c`/`gmcerts` untouched
and reimplement the_Foundation's `iSocket`/`iTlsRequest` over ClassicNet's
`CNTransport` vtable (`cn_darwin8` host/Tiger, `cn_ot` Classic, `cn_tls`
mbedTLS, `CN_TLS_FORCE_TLS12=1`) behind the identical public API, with a
`LAGRANGE_CLASSICNET` compile-time switch.

**Seam state:** `the_Foundation` (`classicnet-seam` branch, pin `f30fdd8`) has
the ClassicNet-backed `iSocket` (N2 step 1, host-tested under ASan); `iSocket`
I/O is selected by `TFDN_CLASSICNET=ON`, which lagrange sets when
`ENABLE_CLASSICNET=ON`. The `iTlsRequest` backend is NOT yet done (N2 step 2).
⚠️ **`the_Foundation`'s `classicnet-seam` branch is LOCAL-ONLY (never pushed)** —
the lagrange pin `f30fdd8` references a local commit, so a fresh clone cannot
reproduce it. Push the branch (and keep the pin in sync) before relying on a
clean checkout, or the seam has nothing to pin to.

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

**N2 step 1 — ClassicNet-backed `iSocket`** (this session, 2026-09-09). The
`the_Foundation` submodule now has a `classicnet-seam` branch with a
`CNTransport` backend for `iSocket`: a `Stream` subclass whose I/O is driven by
a ClassicNet transport (`cn_darwin8` host/Tiger, `cn_ot` OS 8/9) instead of a
raw fd + `select()`, selected by `TFDN_CLASSICNET=ON`. Connected/readyRead/
error/disconnected/bytesWritten/writeFinished audiences fire from the pump.
lagrange wires the seam: `Depends.cmake` turns on `TFDN_CLASSICNET` when
`ENABLE_CLASSICNET`, `ClassicNet.cmake` PUBLIC-links `the_Foundation` to
`classicnet` (which supplies the `CN_HOST`/`CN_WITH_DARWIN8` usage requirements
and include dir), and a loopback unit test + echo TCP server
(`tests/classicnet/t_classicnet_socket.c`, `socket_echo_server.py`,
`scripts/test-classicnet-socket.sh`) validates the audiences and byte framing.

Evidence / reproduce:
- `cmake -S . -B build-classicnet -DENABLE_CLASSICNET=ON -DENABLE_GUI=OFF -DENABLE_HARFBUZZ=OFF -DENABLE_FRIBIDI=OFF`
  then `cmake --build build-classicnet --target t_classicnet_socket -j8` then
  `scripts/test-classicnet-socket.sh ./build-classicnet/t_classicnet_socket`
  → `OK: connected=1 readyRead=1 disconnected=1 error=0 -> 'echo: hello'` (exit 0),
  binary links `libclang_rt.asan_osx_dynamic.dylib`.
- One-shot via ctest: `ctest --test-dir build-classicnet -R classicnet_socket --output-on-failure`.
- Regression gates stay green: stock `app` (build-host) and `canvasapp`
  (build-canvas) rebuild clean.
- `the_Foundation` pin bumped to `f30fdd8` (`classicnet-seam` branch).

## In-flight

1. **N2 step 2 — `TlsRequest` backend** (next; large, monolithic). Reimplement
   `iTlsRequest` over `cn_tls` (mbedTLS, `CN_TLS_FORCE_TLS12=1`): map `submit`/
   `readAll`/`serverCertificate`/`isVerified`/`setVerifyFunc` and the `readyRead`/
   `sent`/`finished` audiences to mbedTLS + `gmcerts`. **Two hard constraints:**
   - *Monolithic symbol set.* `the_Foundation/src/tlsrequest.c` is one file
     providing **both** `iTlsCertificate` and `iTlsRequest`; `gmcerts.c` uses
     nearly every `iTlsCertificate` method (subject/issuer name components,
     alt-names, fingerprints, `verify`/`verifyDomain`, `validUntil`/`isExpired`,
     `pem`, `equal`, `newSelfSignedRSA_TlsCertificate`). A partial port leaves
     undefined symbols once `tlsrequest.c` is swapped out, so the mbedTLS
     replacement must export the full API in one go (~1300+ lines).
   - *mbedTLS cannot generate certificates.* `gmcerts.c` calls
     `newSelfSignedRSA_TlsCertificate` (self-signed test identities) — an OpenSSL
     capability; mbedTLS only parses/verifies. That method needs a fallback/probe
     (cf. the P-3 client-auth/cert work), so a straight API-for-API port is
     impossible.
   **Sub-sequencing:** *(2a)* the `iTlsRequest` transport over `cn_tls` (connect →
   handshake → write content → stream response → `sent`/`readyRead`/`finished`,
   `status`/`isVerified`/`serverCertificate`) + the `iTlsCertificate` core over
   mbedTLS (parse/verify/expiry/`pem`/fingerprint/`verifyDomain`), host-tested
   against a local TLS server with the pinned test CA; *(2b)* the remaining
   `iTlsCertificate` name-component extraction + the self-signed-generation
   workaround. **Gate:** host unit test of the TlsRequest backend against a local
   TLS server, ASan/UBSan clean; `build-host` stock `app` still green.
   Branch stays in the `classicnet-seam` submodule branch.
2. **N3 — into the canvas app.** Wire the seam into `canvaswin` so the viewer
   actually fetches a Gemini page over ClassicNet. Gate: integration test
   fetches a real Gemini URL asserting status/meta/body + the TOFU
   pin/mismatch gate; `canvaswin` shows the page.
3. ClassicNet submodule pin: lagrange is at `8e0df7a` (6-arg `CN_TlsCreate`).
   When the client-cert identity commit (`57ca5db`) lands on `origin`, bump the
   pin and migrate to the 10-arg form as part of the Gemini auth work.
