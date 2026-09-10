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
actual `iTlsRequest` path. The full Aqua canvas host is the next slice.

## Last completed milestone

**T-tier the_Foundation + ClassicNet seam: cross-build + on-device proof (2026-09-10).**
`the_Foundation` (with the ClassicNet socket/tls backends) now cross-compiles for
Tiger PPC, and the seam does a real Gemini fetch on real hardware through the
same `iTlsRequest`/`iTlsCertificate` API `gmrequest`/`gmcerts` use.

Evidence / reproduce:
- `osx/CMakeLists.txt` adds `the_Foundation` (static, `TFDN_CLASSICNET=ON`) →
  `osx/d8_tls_smoke`. New darwin8 shims (arcana): `libunistring 1.4.2`
  cross-built under `vendor/ClassicNet/deps/libunistring-darwin8` (GNU sig
  verified; `AVOID_ANY_THREADS` config fix), `osx/darwin8_posix_shim.h`
  (strnlen/clock_gettime/pthread_setname_np), `osx/darwin8_sdk_shim/`
  (a real `<spawn.h>` over fork/exec for `iProcess`).
- On-device (petal, iMac G4 / OS X 10.4.11 PPC; host capsule `192.168.7.146:1966`):
  `/tmp/d8_tls_smoke 192.168.7.146 1966 /` → `[classicnet] TCP connect` +
  `TLS handshake OK (mbedTLS via cn_tls)` + `d8_tls_smoke OK: status '20
  text/gemini' body=216 certSubj='CN = localhost' isVerified=1`. Evidence:
  `~/classic/petal/logs/lagrange-d8-tfdn-smoke-2026-09-10.txt`.
- `build-osx/d8_tls_smoke` = `Mach-O executable ppc`. (Previous milestone: the
  raw CN-level `d8_smoke`, evidence `lagrange-d8-smoke-2026-09-09.txt`.)
- Toolchain: `~/classic/darwin8-toolchain` (danupsher GCC 15.2 + cctools-port +
  10.4u SDK), docker `ubuntu:24.04` amd64 under Rosetta. See
  `~/.local/share/doc/darwin8-toolchain.md`.

## In-flight

1. **Aqua canvas host (`src/macos/`)** — the T-tier UI: programmatic AppKit
   (`NSMenu` bridge already in `src/platform/macos.m`), one window, the widget
   kit's canvas in an `NSView` via `NSBitmapImageRep`. Manually retain/release,
   10.4-era ObjC, Guarded selectors. Builds → `Gemini.app` bundle via POST_BUILD.
   (The `d8_tls_smoke` fetch is TOFU-accept; the app's real trust gate, and the
   actual `gmrequest`/`gmcerts` app-layer integration, come with this slice.)
2. **N3 tactile confirmation (from Phase 1, still pending)** — the visual check
   that `canvaswin` shows a fetched page + the TOFU trust/mismatch UI on
   QEMU/petal (fetch is proven at the seam + headless-render level).
3. **the_Foundation shim migration** — the darwin8 shims
   (strnlen/clock_gettime/pthread_setname_np/posix_spawn/AVOID_ANY_THREADS)
   are build-time local for now (`osx/*`); migrate into the_Foundation's
   `src/platform/apple.c`/`posix/` behind an OS-version check on the
   `classicnet-seam` branch so they're reusable by the Retro68/M-tier flavor.
4. ClassicNet submodule pin: lagrange is at `f1dbf66` (a local `darwin8-transport`
   commit adding the `[classicnet]` stderr markers — must be pushed for a clean
   clone). The 6-arg `CN_TlsCreate` stays until starscape's client-cert identity
   commit (`57ca5db`) lands on origin; then bump + migrate to the 10-arg form
   as part of the Gemini auth work.
