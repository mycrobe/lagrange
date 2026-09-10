# STATUS

## Where we are

**Phase 1 — ClassicNet network seam (host-first); N1 done, N2 step 1 (Socket)
done, N2 step 2 (TlsRequest) done incl. self-signed generation (2b), N3 wiring
done (canvas build + headless fetch render); remaining (2b) session cache
(non-blocking) + N3 tactile/QEMU page confirmation + ClassicNet pin.**
Phase order is **1) ClassicNet on the host → 2) Tiger/Cocoa → 3) Classic**,
with the cross-build tool systems as parallel enabling work. The seam reuses
the "escape SDL" pattern for networking: keep `gmrequest.c`/`gmcerts` untouched
and reimplement the_Foundation's `iSocket`/`iTlsRequest` over ClassicNet's
`CNTransport` vtable (`cn_darwin8` host/Tiger, `cn_ot` Classic, `cn_tls`
mbedTLS, `CN_TLS_FORCE_TLS12=1`) behind the identical public API, with a
`LAGRANGE_CLASSICNET` compile-time switch.

**Seam state:** `the_Foundation` (`classicnet-seam` branch, pin `f30fdd8`; the
branch IS now pushed to `origin/classicnet-seam` — the local-only caveat from
before is resolved, but the step-2 commit is still uncommitted in the submodule
working tree and must be committed + pinned before a clean checkout) has both
ClassicNet backends: the `iSocket` (N2 step 1) and the `iTlsRequest` +
`iTlsCertificate` over `cn_tls`/mbedTLS (N2 step 2, host-tested under ASan).
Both I/O paths are selected by `TFDN_CLASSICNET=ON`, which lagrange sets when
`ENABLE_CLASSICNET=ON`; otherwise the stock OpenSSL `src/tlsrequest.c` and
`posix/socket.c` are used.

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

**N2 step 2 — ClassicNet-backed `iTlsRequest` + `iTlsCertificate` over `cn_tls`
(mbedTLS)** (this session, 2026-09-09). A new
`the_Foundation` classicnet backend (`src/platform/classicnet/tlsrequest.c`,
selected by `TFDN_CLASSICNET`) reimplements the full `tlsrequest.c` public API
(`iTlsCertificate` + `iTlsRequest`) over mbedTLS via ClassicNet's `cn_tls`:
the request wraps a `cn_darwin8` socket in `CNTlsTransport`, drives the
non-blocking TLS handshake on a single worker thread, streams the plaintext
request (firing `sent`) and decrypted response (firing `readyRead`), and ends
with `finished`; the server certificate is captured from the handshake and
served via `serverCertificate_TlsRequest()`. The TOFU gate is enforced with an
mbedTLS verify callback that consults the app's `iTlsRequestVerifyFunc` for the
leaf (a cert mbedTLS verifies via CA/hostname is accepted without the callback;
a failed cert defers to the app, and a rejection aborts the handshake and marks
`isVerified()==false`). `iTlsCertificate` is fully implemented over mbedTLS:
parse/verify/expiry/`pem`/fingerprints/domain+IP+wildcard match/alt-names/
subject+issuer (formatted OpenSSL-ONELINE style, matching what `gmcerts`'
`misfinIdentity`/`name_GmIdentity` parse)/copy/equal/hasPrivateKey/keys.
`newSelfSignedRSA_TlsCertificate` (mbedTLS cannot *generate* certs) is stubbed
to return an empty cert with a warning — that is the remaining (2b) workaround.

Evidence / reproduce:
- `cmake -S . -B build-classicnet -DENABLE_CLASSICNET=ON -DENABLE_GUI=OFF -DENABLE_HARFBUZZ=OFF -DENABLE_FRIBIDI=OFF`
  then `cmake --build build-classicnet --target t_classicnet_tls -j8` then
  `scripts/test-classicnet-tls.sh ./build-classicnet/t_classicnet_tls`
  → `[ca] OK ... verifystatus=2 isVerified=1` and `[tofu] OK ... verifystatus=0 isVerified=1`
  (CA-verified fetch + TOFU-accepted fetch), exit 0.
- One-shot via ctest: `ctest --test-dir build-classicnet -R classicnet_tls --output-on-failure`.
- Full classicnet gate: `ctest --test-dir build-classicnet -R 'classicnet_'`
  → n1 (ClassicNet 13/13 + smoke), socket, tls all pass.
- Regression gates stay green: stock `app` (build-host) and `canvasapp`
  (build-canvas) rebuild clean.
- `the_Foundation` seam work is uncommitted in the submodule (step 1 + step 2);
  commit and bump the pin before a clean checkout.

## In-flight

1. **(2b) — is complete except the TLS session cache.** The self-signed
   certificate *generation* is now implemented over mbedTLS (the X.509 write
   module + RSA keygen), so `newIdentity_GmCerts` produces a real self-signed
   identity cert instead of an empty stub: it generates an RSA keypair, builds
   subject/issuer names from the `iTlsCertificateName` array (CN, emailAddress,
   domain-as-DC, OU, O, C; UID via the numeric-OID DER-hex form since mbedTLS's
   parser has no "UID" short name), sets a validity window, signs SHA-256 with
   `mbedtls_x509write_crt_der` (writes the DER backwards — parse from
   `buf+size-len`), and re-parses it into an `iTlsCertificate` with the private
   key attached. Host-verified (`[selfgen] OK` in the TLS test). RNG is
   `mbedtls_entropy_func` (on Classic the platform RNG must be supplied — the
   documented ClassicNet hard problem).
   * Remaining (non-blocking, correctness-neutral): the TLS *session cache*
     (`setSessionCacheEnabled_TlsRequest` is a no-op over `cn_tls`);
     `saveSession_Context_`/`CachedSession` are OpenSSL-only and were not carried
     over, so each request re-negotiates a session instead of reusing one.
2. **N3 — seam into the canvas app: wiring done, tactile page confirmation
   pending.** The canvas app now builds with the seam:
   `cmake -S . -B build-canvas-classicnet -DENABLE_CANVAS=ON -DENABLE_CLASSICNET=ON ...`
   → `canvasapp` (headless) and `canvaswin` (windowed) both link the ClassicNet
   the_Foundation (the app's portable `gmrequest`/`gmcerts` use
   `new_TlsRequest`/`new_Socket` unchanged, which route to ClassicNet). A headless
   run of `canvasapp` against a local TLS Gemini server (SDL dummy driver,
   isolated `CANVAS_PREF_DIR`, TOFU trust pre-seeded in `trusted.2.txt`) renders a
   900x560 page frame with no cert/trust error, and the seam-level fetch gate now
   covers CA-verify + TOFU-accept + TOFU **mismatch/reject** in
   `tests/classicnet/t_classicnet_tls.c` (`[ca] [tofu] [selfgen] [reject]` all OK).
   * Build gotcha (see `docs/arcana.md`): an app build (GUI) must set
     `-DCN_SANITIZE=OFF` — ClassicNet compiles its lib with ASan by default
     (`CN_SANITIZE`), but the app targets aren't linked with the sanitizer
     runtime, so linking ASan-instrumented `libclassicnet.a` into `canvaswin`
     fails on `___asan_init` otherwise.
   * Tip for eyeballing the seam live: ClassicNet now logs one line to stderr per
     transport (`[classicnet] TCP connect <host>:<port> (cn_darwin8)` and
     `[classicnet] TLS handshake OK (mbedTLS via cn_tls)`) — run any lagrange or
     canvas build over the seam and watch them appear as you browse (the app log
     is `/tmp/kilo/cn.log` in the isolated state-dir runs). Closes the "is it
     really using ClassicNet?" question with a visible, per-fetch marker.
   * Remaining N3 gate: the *visual* confirmation that `canvaswin` shows the
     fetched page and the TOFU trust/mismatch *UI* prompt — the tactile/QEMU/
     real-hardware check (the fetch itself is proven at the seam level and by the
     headless frame render).
3. ClassicNet submodule pin: lagrange is at `f1dbf66` (a local
   `darwin8-transport` commit on top of `8e0df7a` that adds the `[classicnet]`
   stderr markers — must be pushed for a clean clone). The 6-arg `CN_TlsCreate`
   stays until the client-cert identity commit (`57ca5db`) lands on `origin`,
   then bump and migrate to the 10-arg form as part of the Gemini auth work.
