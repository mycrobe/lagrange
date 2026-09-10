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
network seam is confirmed portable to the T-tier. The full Aqua canvas host and
the the_Foundation seam cross-build are the next slices.

## Last completed milestone

**T-tier darwin8 network slice: cross-build + on-device proof (2026-09-09).**
The ClassicNet darwin8 slice + mbedTLS-3.6 (TLS 1.2 pinned) now cross-compile
for Tiger PPC and do a real Gemini fetch on real hardware.

Evidence / reproduce:
- Cross-build pipeline: `scripts/build-osx.sh` (docker amd64 →
  `powerpc-apple-darwin8-gcc`, `-Wl,-force_cpusubtype_ALL` + `-lresolv` on the
  link line, configure-time 64-bit-division link probe) →
  `build-osx/d8_smoke` (`Mach-O executable ppc`; only `libSystem.B.dylib` +
  `libresolv.9.dylib`).
- On-device (petal, iMac G4 / Mac OS X 10.4.11 PPC; host capsule on
  `192.168.7.146:1965`): `/tmp/d8_smoke 192.168.7.146 1965 /` → `[classicnet]
  TCP connect` + `TLS handshake OK (mbedTLS via cn_tls)` + `d8_smoke ok ...
  bytes=251` with the `20 text/gemini` capsule body. Evidence:
  `~/classic/petal/logs/lagrange-d8-smoke-2026-09-09.txt`.
- mbedTLS-d8 built by `vendor/ClassicNet/scripts/setup-mbedtls.sh`'s darwin8
  step (gitignored under `vendor/ClassicNet/deps/mbedtls-darwin8`).
- Toolchain: `~/classic/darwin8-toolchain` (danupsher GCC 15.2 + cctools-port +
  10.4u SDK), docker `ubuntu:24.04` amd64 under Rosetta. See
  `~/.local/share/doc/darwin8-toolchain.md`.

## In-flight

1. **the_Foundation + seam cross-build for darwin8** — the big next slice: get
   `the_Foundation` (with `TFDN_CLASSICNET` socket/tls backends) to cross-compile
   for Tiger, so lagrange's actual `gmrequest`/`gmcerts` fetch path ports. Legacy
   Open Question (PLAN.md): which the_Foundation modules compile as-is vs shim on
   darwin8 (expect poll/pthread to exist on Tiger; verify `-lresolv` + the
   64-bit-division quirk don't recur).
2. **Aqua canvas host (`src/macos/`)** — the T-tier UI: programmatic AppKit
   (`NSMenu` bridge already in `src/platform/macos.m`), one window, the widget
   kit's canvas in an `NSView` via `NSBitmapImageRep`. Manually retain/release,
   10.4-era ObjC, Guarded selectors. Builds → `Gemini.app` bundle via POST_BUILD.
3. **N3 tactile confirmation (from Phase 1, still pending)** — the visual check
   that `canvaswin` shows a fetched page + the TOFU trust/mismatch UI on
   QEMU/petal (fetch is proven at the seam + headless-render level).
4. ClassicNet submodule pin: lagrange is at `f1dbf66` (a local `darwin8-transport`
   commit adding the `[classicnet]` stderr markers — must be pushed for a clean
   clone). The 6-arg `CN_TlsCreate` stays until starscape's client-cert identity
   commit (`57ca5db`) lands on origin; then bump + migrate to the 10-arg form
   as part of the Gemini auth work.
