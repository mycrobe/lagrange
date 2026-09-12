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

### the_Foundation on darwin8 (Tiger) — the db-verified POSIX gaps

Porting the_Foundation (lagrange's portable core) to Tiger cross-build
surfaced four real gaps, all "core assumes modern macOS/POSIX" — the
legacy Open Question "which modules compile as-is vs shim" got a concrete
answer:

- **`libunistring` is a MANDATORY the_Foundation dep** (`Depends.cmake`
  `FATAL_ERROR`s without `unistr.h`; `string.c`/`punycode.c` call
  `u8_normalize`/`u8_mbsnlen`/`u8_check`/`u8_to_u32`). It is NOT optional
  and is absent from PLAN.md Phase-2's dep list — add it. Cross-build the
  GNU tarball (`--host=powerpc-apple-darwin8 --disable-shared
  --enable-static`) into `vendor/ClassicNet/deps/libunistring-darwin8`
  (gitignored, like mbedtls-d8). **Verify the tarball against GNU's
  detached `.sig`** — `gpgv --keyring gnu-keyring.gpg <t>.tar.xz.sig <t>.tar.xz`
  (fetch the keyring from ftp.gnu.org; `gpg` isn't on this box, `gpgv` is).
- **libunistring's gnulib thread detection fails on Tiger**: the 10.4u SDK
  exports `pthread_create` as an *inline*, so gnulib concludes "no real
  pthread API" (`gl_pthread_api=no`, `PTHREAD_CREATE_IS_INLINE=1`) and
  `lib/mbtowc-lock.h` matches **no** branch — leaving `mbtowc_with_lock`
  undefined and both `mbrtowc.c`/`mbrtoc32.c` failing to compile
  (`implicit declaration`). `--enable-threads=posix` does NOT fix it (the
  underlying pthread link probe is what's fooled). The fix: define
  `AVOID_ANY_THREADS 1` in the generated `config.h` — that routes
  `mbtowc-lock.h` to the self-contained no-lock branch. (This makes
  libunistring's own mbrtowc single-threaded; the_Foundation uses UTF-8
  strings so the mbtowc path isn't exercised, and this is a stopgap
  pending the proper threadlib cache override.) **[2026-09-10]**
- **`strnlen` / `clock_gettime` / `pthread_setname_np`** — all absent on
  Tiger (strnlen ≥10.7, clock_gettime ≥10.12, Apple pthread_setname_np
  ≥10.6). the_Foundation's `string.c`/`time.c`/`thread.c` use them
  unconditionally. Shim: `osx/darwin8_posix_shim.h`, force-included
  (`-include`) into ONLY the darwin8 the_Foundation build via
  `-DDARWIN8`; each is `static inline` (strnlen over `memchr`,
  clock_gettime over `gettimeofday`, pthread_setname_np no-op). If the
  shim only reaches the_Foundation's own TUs, nothing leaks into the
  final link. **[2026-09-10]**
- **`<spawn.h>` (posix_spawn)** — absent on Tiger. `platform/posix/process.c`
  includes it unconditionally for the `iProcess` class. Provide a REAL
  implementation (`osx/darwin8_sdk_shim/spawn.{h,c}`) over fork+execve
  (close/dup2 file actions), not a declaration-only stub — a fake header
  that only satisfies the compile would silently break `iProcess` (e.g.
  OpenURL) at the full app's link. Added PRIVATE to the_Foundation as an
  include dir + one source. **[2026-09-10]**

These shims are candidate material to migrate into the_Foundation's
`src/platform/apple.c`/`posix/` behind an OS-version check, on the
`classicnet-seam` branch. `d8_tls_smoke` (osx) is the on-device proof.

**Mandatory deps revealed by the full app (the Aqua host, 2026-09-10):** the
seam-only `d8_tls_smoke` never touched `iRegExp`/`iArchive`, so PCRE2 and zlib
were invisible. The full widget kit needs both:

- **PCRE2 10.47 (`deps/pcre2-darwin8`)** — `gmdocument.c` calls
  `replaceRegExp_String`/`iRegExp`; `the_Foundation` only builds `regexp.c`
  when PCRE2/PCRE is found (`iHaveRegExp` → `regexp.h` APIs). Cross-build the
  tarball `--host=powerpc-apple-darwin8 --disable-shared --enable-static
  --enable-unicode --disable-pcre2grep --disable-pcre2test` (sig-verified via
  gpg; the GitHub release is signed by a PCRE2 release manager). **Bug:**
  `the_Foundation`'s `tfdn_link_depends()` adds PCRE1 include dirs but only
  PCRE2 *libs* (its include-dir block omits `PCRE2_INCLUDE_DIRS`) — so
  `regexp.c` can't find `pcre2.h`. Workaround: `target_include_directories(
  the_Foundation PUBLIC ${PCRE2_INCLUDE_DIRS})` + `target_link_directories(
  ... PUBLIC ${PCRE2_LIBRARY_DIRS})` in the consumer's CMake; the clean fix
  belongs in the the_Foundation fork. **[2026-09-10]**
- **zlib (`iHaveZlib`)** — `archive.h` only declares `iArchive` under
  `iHaveZlib`; resources/fontpack need it. Tiger ships zlib in libSystem, so
  no cross-build: the cross container lacks pkg-config, so give it a module.
  `osx/pkgconfig/zlib.pc` (points at the 10.4u SDK `usr/lib`, `-lz`) +
  install `pkg-config` in the container. `scripts/build-osx.sh` now carries
  `PKG_CONFIG_PATH=/work/vendor/ClassicNet/deps/pcre2-darwin8/lib/pkgconfig:/work/osx/pkgconfig`.
  `iHaveZlib` may also be force-cached as a belt-and-suspenders. **[2026-09-10]**

**Aqua host on-device run — the resource-archive blocker, ROOT CAUSE FOUND
(2026-09-10).** The `L4.app` cross-build is proven (PPC Mach-O), and launched
on petal it reached the AppKit UI-construction stage (the run's autorelease spam
shows NSFont/NSImage/NSView being created for the window's content → the
WindowServer connection works). It then aborted at resource loading:

```
failed to load resources: Unknown error: 0    (errno 0; init_Resources returns iFalse)
```

`init_Resources()` → `openFile_Archive()` → `readDirectory_Archive_()` failed on
the PPC target while the *identical* `resources.lgr` (a valid zip) loaded fine on
the host canvas app. The version gate is NOT the cause (`init_Version` drops the
dev/app suffix, so the lgr's `VERSION` matches). **Root cause: an endianness-guard
typobug in `the_Foundation`'s stream.c.** `config.h.in` emits `iHaveBigEndian`
(from CMake's `test_big_endian()`, CMakeLists.txt:158), but the byte-order
selection in `stream.c:54` tested `#if defined (iBigEndian)` — a macro that is
NEVER defined anywhere. So every build, including the big-endian PPC, compiled
the *native-little-endian* ordering functions. On the PPC target the defaults are
mirrored (`order32le_` etc. swap), so a little-endian ZIP's central directory and
EOCD signature were read byte-swapped: `seekToCentralEnd_()` never matched
`SIG_END_OF_CENTRAL_DIR` and `readDirectory_Archive_()` returned iFalse. It never
surfaced before because `d8_tls_smoke` never touched `iArchive`, and the host
(x86, little-endian) is unaffected. **Fix:** change the guard to
`#if defined (iHaveBigEndian)` (the_Foundation `classicnet-seam` `005d88b`).
A focused on-device reproducer (`osx/d8_archive_smoke.c`) now opens the same
`resources.lgr` on petal, parses all 62 entries (correct names/sizes, deflate
method), and inflates a deflated entry — evidence
`~/classic/petal/logs/lagrange-d8-archive-tfdn-2026-09-10.txt`. The embedded
`iFile`/`iStream`/ZIP path is byte-order-correct on Tiger. `logs/aqua-petal-runtime-2026-09-10.txt`
is the (pre-fix) run evidence. Also note the execPath quirk: app.c builds
`execPath` by appending argv[0]'s basename to `SDL_GetBasePath()`, so
`execPath/../resources.lgr` is always ENOTDIR — a deployed bundle needs an
absolute `LAGRANGE_EMB_BIN` (`-DAQUA_EMB_BIN=/path/resources.lgr`, the host
canvas uses an absolute path too). **⚠️ trap (hit 2026-09-11):** `osx/CMakeLists.txt`
defaults `AQUA_EMB_BIN` to the dev-relative `"resources.lgr"` (fine for a host
canvas run, useless for a deployed bundle). A *clean* reconfigure (`rm -rf
build-osx` + `cmake` without `-DAQUA_EMB_BIN=...`) silently bakes that relative
string back in, so the freshly-rebuilt `.app` starts, prints
`failed to load resources: No such file or directory`, and `exit(-1)s` the
moment `init_Resources` runs — the app never stays up. Every rebuild meant for
petal must pass `-DAQUA_EMB_BIN=/tmp/L4.app/Contents/MacOS/resources.lgr`
(i.e. the deployed absolute path). `scripts/build-osx.sh` does NOT pass it, so a
bare `build-osx.sh` after a clean is NOT deployable as-is.

**Finder/LaunchServices injects a `-psn_<serial>` argv — strip it in the Aqua
main (durable).** Mac OS X GUI apps launched from Finder/`open` receive a
`-psn_0_<pid>` argument identifying them to the WindowServer; SDL's own backend
normally swallows it, but this canvas host drives `run_App()` with its raw argv,
and the portable CommandLine parser rejects it as `Unknown option: p` and
`terminate_App_(1)`s — the app dies instantly with no window. `src/macos/aquamain.c`
filters `-psn_*` out of argv before `run_App`. On Tiger `open` also refuses a
bare-exec (WindowServer: `bootstrap_register` 1100), and `launchctl setenv`
does NOT propagate to a LaunchServices-launched app; bake env vars into the
bundle's `Info.plist` `LSEnvironment` instead (e.g. `AQUA_ERRLOG` → the app-owned
logfile, `AQUA_DEBUG`). `osx/Info.plist`'s `LSEnvironment` is added at deploy
time by editing the deployed plist (Tiger has no `PlistBuddy`; use perl).
**[2026-09-10]**

**Tiger's `open` has NO `--args` — a bundle-launched app takes a URL only via a
`kAEGetURL` AppleEvent, so register the scheme (durable, 2026-09-10).** On Tiger
(10.4) `open --args gemini://...` is not supported: `open` treats `--args` (and
`-h`) as a file path and errors `No such file: /Users/<user>/--args`. A bundle-
launched GUI app therefore cannot receive a positional URL argv — the widget
kit's argv path only ever sees the `-psn_` arg (stripped above). The real
`macos.m` host solves this by registering a `kAEGetURL` handler
(`registerURLHandler_MacOS`) + declaring `CFBundleURLTypes`, so a Finder
`open gemini://host/path` delivers the URL as an AppleEvent. The Aqua host is a
separate lean host and lacks this — so add it: `src/macos/aquaview.m` gets an
`AquaURLHandler` (`registerUrlHandler_Aqua`, sets the `kInternetEventClass`/
`kAEGetURL` handler, posts `~open newtab:1 url:%s` after `urlDecodeExclude_String`),
and `osx/Info.plist` declares `CFBundleURLTypes` for `gemini`/`gopher`/`gophers`/
`spartan`. Call `registerUrlHandler_Aqua()` in `aquamain.c` BEFORE `[NSApp run]`
(registered right after `initAquaView_app`, so a launch URL is live when the run
loop starts). Registering the scheme is also what makes a fresh `open
gemini://...` spin up the app from a URL at all. With this, the drive recipe for
an on-device fetch is: `open /tmp/L4.app` then `open gemini://host/path` (the
second `open` forwards to the already-running instance via the handler). The
single-instance IPC means a URL `open` while another instance is running is
deferred to it, and the app-owned log overwrites per launch — kill by PID
(`ps -Ao pid,command | grep /tmp/L4.app`, `kill -9`) between runs so a fresh
instance + fresh log. Evidence: `~/classic/petal/logs/l4-aqua-tofu-git-fetch-2026-09-10.png`
+ `-tofu-capsule-...png` + `-tofu-mismatch-...png` + `-tofu-runtime-...txt`.
**[2026-09-10]**

**Aqua host must run `[NSApp run]` for the native menu bar, and step the widget
kit from a timer (durable).** A bare `NSApplication` (no nib) driven by a manual
pump hook (`nextEventMatchingMask:untilDate:0` = an immediate non-blocking poll)
NEVER runs AppKit's main loop, so on Tiger the OS menu bar is not populated —
`setMainMenu:` with a fully-built `NSMenu` (verified via a hardcoded probe) still
shows only the app-name stub; re-activating the app and short run-loop drains
don't help. The real fix is to let `[NSApp run]` own the main thread and drive
the widget kit from a ~60Hz `NSTimer`: this is why `run_App` was split into
`init_App` + `beginAppEventLoop_App` + `step_App(eventMode)` + `deinit_App_Instance`
(`src/app.c`/`app.h`); `aquaview.m`'s `runAquaMainLoop` installs the timer
(calling `step_App(postedEventsOnly_AppEventMode)`) and calls `[NSApp run]`.
Quitting: the widget kit's `SDL_QUIT`/`"quit"` just flips `isRunning` (a
`step_App` loop var), it does NOT unwind `[NSApp run]` — the timer checks
`isAppRunning()` and calls `[NSApp terminate:]` (this exits via AppKit, so
`main()`'s normal teardown never runs; `[NSApp stop:]` doesn't reliably unwind
the run loop). Because the host exits through AppKit, `deinit_Foundation()` is
registered with `atexit` in `aquamain.c` (atexit runs last-registered-first, so
it cleans up before the_Foundation's own atexit check) — without it, quitting
emits a `!isInitialized_Foundation()` at-exit assertion.
Cmd-shortcuts: macOS routes Cmd-combos through `performKeyEquivalent:` before
`keyDown:`; the native menu bar renders but its `NSMenuItem` key equivalents do
NOT fire under the `[NSApp run]`+timer model (unless the host explicitly asks),
so the Aqua window's `performKeyEquivalent:` first tries
`[[NSApp mainMenu] performKeyEquivalent:event]` (menu key equivalents win) then
forwards unhandled Cmd-combos to the widget kit as SDL key events. The app menu
is `mainMenu[0]` (must exist at init or the widget kit's `insertItem` at index 1
crashes): the backend creates it titled `L4` and populates About/Preferences/Quit.
**Tiger/Leopard needs an explicit `setAppleMenu:` nomination (durable, 2026-09-10).**
On Tiger (10.4) the initial belief that titling index 0 "L4" makes AppKit merge it
into a single app-process menu is WRONG (verified on petal — the menu bar showed a
bold app-name `L4` AND a second plain `L4`). Pre-SnowLeopard AppKit does NOT treat
a programmatically-set main menu's first item as the application menu; a nibless
app gets AppKit's own synthesised bold app-name menu prepended, so the backend's
own index-0 `L4` lands as a second, plain menu beside it. The fix (and the exact
workaround documented for Leopard): call the long-unannounced-but-still-live
`-[NSApplication setAppleMenu:]` with the index-0 submenu, declaring the selector
in a category (`canvasmenu_impl_aqua.m`) — the same selector the nib loader calls
to nominate the app menu. Snow Leopard (10.6) auto-identifies `mainMenu[0]` and
dropped the need for it, which is exactly why lagrange's own modern menu code
(`macos.m`, `canvasmenu_impl_SDL.m`) works without ever calling it: that modern
path `[[[NSApp mainMenu] itemAtIndex:0] submenu]` and assumes 10.6+ app-menu
detection, and must not be cargo-culted onto the pre-10.6 Aqua host. Evidence:
`~/classic/petal/logs/l4-aqua-menubug-2026-09-10.png` + `-menubug-...txt`.

**Tiger has no `popUpMenuPositioningItem:atLocation:inView:` (10.6+) — context
menus need the originating NSEvent (durable, 2026-09-11).** The modern mac
menu path pops a context menu with the 10.6+ `popUpMenuPositioningItem:
atLocation:inView:` (see `canvasmenu_impl_SDL.m`). That selector does not exist
on pre-10.6, so the Aqua host must use the (older, still-present)
`+[NSMenu popUpContextMenu:withEvent:forView:]`, which takes the originating
NSEvent. The widget kit's `showPopupMenu_MacOS` only forwards a windowCoord +
the item array (no NSEvent), so the Aqua view remembers the latest mouse-down
`NSEvent` (`setAquaPopupEvent_Aqua`/`currentAquaPopupEvent_Aqua` in aquaview.m,
exposed as void* in aquaview.h to stay C-clean) and the menu backend uses it
with the canvas view (`aquaMainView_Aqua`) as `forView`. The `forView` only
needs to be a view in the event's window; `gView_` is the window's contentView.
Reentrancy is not an issue: the Aqua tick timer is scheduled in
`NSDefaultRunLoopMode`, which does not fire during the menu-tracking run-loop
mode, so `step_App` is not re-entered while the popup is up. `popUpContextMenu`
blocks modally until dismissed and needs a real (physical) right-click to
trigger — System Events UI-scripted clicks are gated over SSH on this host.

**Tiger's menu font can't render Lagrange's icon glyphs — native menus must
strip them (durable, 2026-09-11).** The `*_Icon` codepoints (defs.h) mostly live
in supplementary planes / symbol blocks the system menu font (Lucida Grande)
lacks, so a native `NSMenu` label shows them as tofu (a black box, or the
compact `">>>" backArrow_Icon` nav items showed as `>>> ◼`). The real mac host
already stops them reaching NSMenu (`removeIconPrefix_String` in
`src/platform/macos.m`); the canvasmenu Aqua host now does the same
(`nativeMenuLabel_` in `canvasmenu_impl_aqua.m`: strips the `###`/`///`/```
markers, drops the leading icon glyph + trailing space, and removes colour
escapes). Native menus are text-only — that's the intended mac look (icons only
render in the in-window/text-engine menus). Related: the document context-menu
nav items ("Go Back" etc.) carry text only in the Apple branch of
`documentwidget.c` (the `iPlatformApple && LAGRANGE_ENABLE_MAC_MENUS` gate);
`iPlatformApple` is NOT defined on the darwin8 cross-build and it gates Apple
return-key semantics in defs.h — too risky to define just for this — so the
Aqua host reaches that text-bearing branch via `|| defined (LAGRANGE_NATIVE_MENU)`.

**Native checkmarks: the widget kit only marks `###` in the open/dropdown path,
not the always-visible native menu bar (durable, 2026-09-11).** `setSelected_`
`NativeMenuItem` (the `###`/"checked" prefix) runs when a native menu is opened
via `openMenuFlags_Widget`/dropdown selection, so context menus + dropdown
menu buttons automatically get native checkmarks.  The top-level menu BAR,
though, is built once from static arrays (`window.c` `*MenuItems_` →
`insertMenuItems_MacOS`) with no marking + no re-populate, so its toggles never
showed a check via that path.  The Aqua host mirrors it itself:
`markSidebarModeCheck_MacOS` (canvasmenu_impl_aqua.m) walks every top-level
submenu for items whose target command is `sidebar.mode arg:N toggle:1` and
sets `NSOnState` on the one matching the active left-sidebar mode, driven from
`handleCommand_MacOS` on `sidebar.mode.changed`.  But the sidebar's *initial*
`setMode_SidebarWidget` (sidebarwidget.c init) does not post that event, and
`findWidget_App("sidebar")` is NULL during the menu-bar build (window/menu init
runs before the sidebar is created), so a build-time mark can't see it.  Fix:
`sidebarwidget.c` now posts `sidebar.mode.changed arg:N` (side-appropriate) right
after the initial `setMode` — `postCommand_App` queues it, so it's dispatched
once init completes and the View menu exists — and `root.c`'s toolbar handler
was NULL-guarded for that queued event.  Verified on-device (System Events):
"Show Bookmarks" checked at launch → live-moved to "Show Feed Entries" after
clicking it.  Note: Tiger's System Events reports a menu item's `value` as
`missing value` regardless of check state — read the checkmark visually (or via
`AXMenuItemMarkChar`), not via `value`.

**The darwin8 gcc mangles non-ASCII in ObjC `@"..."` literals — a compiler
charset bug, NOT a Tiger rendering limit (durable, 2026-09-11).** The app-menu
item "Preferences…" showed as `Preferences‚Ä¶` on petal. This is NOT Tiger
failing to render `…` (U+2026 renders fine, and the *lang-string* `…` items
were always correct): the old Apple gcc (`powerpc-apple-darwin8-gcc`, cc1)
decodes an ObjC `@"..."` string literal with the **MacRoman execution
character set**, so any non-ASCII bytes in the literal -- and even a
`@"\uXXXX"` escape -- come out mangled. Proven on petal with a minimal PPC
probe (compiled with the real cross-gcc, run on the G4):
`@"Preferences…"` and `@"Preferences\xe2\x80\xa6"` both decode to
U+201A U+00C4 U+00B6 (`‚Ä¶`, len 14, not U+2026), and `@"\u2190"` decodes to
U+201A U+00DC U+00EA. By contrast `[NSString stringWithUTF8String:
"Preferences\xe2\x80\xa6"]` and `[NSString stringWithCString:encoding:
NSUTF8StringEncoding]` yield the correct U+2026, and the NSString survives
unchanged into an NSMenuItem title. **Rule: never put a non-ASCII character
(or `\uXXXX` escape) in an ObjC `@"..."` literal in the darwin8 sources;
build the string from UTF-8 bytes at runtime via `stringWithUTF8String:`**
(`utf8String_` helper in `canvasmenu_impl_aqua.m`). Non-ASCII in a plain C
string literal is fine (bytes pass verbatim to the UTF-8 decoder). Verified:
the rebuilt L4 binary contains the correct UTF-8 `Preferences…` and no
MacRoman-mangled sequence. `\xc9` in a literal is MacRoman `…`, but use the
runtime helper, not byte escapes.

**Tiger AppKit under `[NSApp run]` + a timer needs explicit autorelease pools, and
the app-owned log must be unbuffered (durable, 2026-09-10).** A nibless Aqua host
driven by `[NSApp run]` (aquaview.m `runAquaMainLoop`) + a ~60Hz `NSTimer` calling
`step_App()` floods stderr with `_NSAutoreleaseNoPool` (`NSAutoreleasePool no pool
in place`) — AppKit does NOT supply an automatic pool for every run-loop event the
way modern macOS does, and `step_App` (event dispatch + render + present) and the
menu/UI assembly autorelease a steady stream of Foundation/AppKit objects (NSFont,
NSImage, NSCFString, NSCFArray, NSCFDate, NSCFTimer …) that then never release.
Fix is twofold: (1) `main()` wraps its whole body in an NSAutoreleasePool frame
(exposed to the C `aquamain.c` as `beginAutoreleasePool_Aqua`/`endAutoreleasePool_Aqua`,
since C can't build a pool), so every main-thread autorelease has a home; and
(2) `tick:` uses its own per-frame pool so the timer-driven churn drains every frame
instead of accumulating in the outer pool. Verified: 614 `_NSAutoreleaseNoPool`
lines before, 0 after (evidence `l4-aqua-poolfix-2026-09-10.txt`).
Also: after `freopen(errLog, "wb", stderr)` the FILE is fully buffered and the
`[aqua]` stage lines sit in a 4KB buffer — a Finder-launched app that crashes never
flushes them, killing the post-mortem story. Call `setvbuf(stderr, NULL, _IONBF, 0)`
after the freopen so the stage markers (and any error) hit the disk immediately.
Bit-rot trap: `killall`/`pkill` may not reap a straggler `./L4` (single-instance per
user — a fresh launch forwards its args to the running one and silently exits); kill
by PID (`ps aux | grep L4 | awk '{print $2}'` → `kill -9`) before relaunching.
`[2026-09-10]`
*Retro68/68k note: the M-tier Toolbox host has no such pool problem (no NSObject
autorelease; the Menu Manager is manual), so this is Aqua/T-tier only.*

**Mouse delivery needs `acceptsFirstMouse:` YES (durable, 2026-09-10).** Under a
nibless `[NSApp run]`+timer window, Tiger swallows the first click on a window that
is not *key* as an activation click (default `acceptsFirstMouse: NO`); the window's
key status isn't settled before the user's first click, so every click can be
consumed and no mouse event reaches the content view (while the menu bar and
keyboard shortcuts still work, since they don't need the key window). Fix:
`AquaCanvasView -acceptsFirstMouse:` returns YES. Coordinates must also map view →
shim canvas; `sdlPoint` and `drawCanvasInto` share a `canvasRectInView`
letterbox transform so clicks stay correct when the window is resized (before this,
a resized window ≠ 900x560 → clicks/hover missed widgets).
**Untagged shim events must target the *presented* window, not the last created
one (root cause of "mouse dead but keyboard fine", 2026-09-10).** The Aqua host
synthesizes shim SDL events and leaves `windowID == 0`; `SDL_PushEvent()` filled
it in with `g_nextWindowId` (*last created* window). Lagrange creates several
windows at startup, but the canvas host only ever presents window index 0
(`SDL_RenderPresent` → `presentHook_(0)`; the Aqua host blits index 0), and the
main/visible window is the first one created. So mouse motion/buttons were tagged
to a later, hidden window whose `DocumentWidget` was empty
(`documentBounds.size.y == 0`, `visibleLinks == 0`), and `view->hoverLink` could
never be set — clicks reached a document that had nothing to click. **Keyboard
events were unaffected because key events are not in the window-tagging branch at
all (windowID stays 0, and `dispatchEvent_Window` only filters when it is
non-zero)** — the menu bar and Cmd shortcuts kept working while the mouse looked
dead, which is what made this so misleading (and wrongly fingered the
`[NSApp run]`+timer event loop). Fix: tag with `g_windows[0]->id`. This also
*removes* the "motion accumulation / `iPlatformApple`" theory: with routing
fixed, hover updates normally and the shared widget-kit code paths (identical on
the host) behave correctly. On-device diagnostics: Tiger's WindowServer drops
`CGEventPost` from an SSH session, so a synthetic system cursor cannot drive the
app remotely; use the gated in-app self-test `AQUA_SELFTEST="cx,cy"`
(`AQUA_SELFTEST_DELAY`, `AQUA_SELFTEST_IMMEDIATE`) in `aquaview.m`, with
`AQUA_MOUSEDBG=1` to trace the `[aqua]`/`[dw]`/`[hover]`/`[shim] PushEvent` legs.
Evidence: `~/classic/petal/logs/l4-aqua-mouse-2026-09-10.txt` (old, pre-fix) and
`l4-aqua-linkfix-2026-09-10.png`/{txt}. **[2026-09-10]**
**[2026-09-10]**
NOT `iBigEndian`. Any `#if defined (iBigEndian)` in the_Foundation source is a
no-op on all builds (the macro is never defined), silently forcing the
native-little-endian path — byte-swap correct on x86, WRONG on any big-endian
target (PPC/PPC64). Big-endian builds that read little-endian streams (ZIP
archives, `serialize_*`/`deserialize_*` persisted data) fail to parse. This is
the classic "works on the host, breaks on the target, no crash just garbage"
class. If a future big-endian port (M-tier OS 9 is also 68k/PPC big-endian)
misbehaves reading persisted blobs, check this guard first. **[2026-09-10]**

### libunistring cross-build — the full recipe (darwin8 + Retro68)

`libunistring` is a MANDATORY the_Foundation deps (see above) and is absent from
PLAN.md Phase 2's dep list. Both tiers now cross-build it via
`scripts/setup-libunistring.sh [retro68|darwin8]` (toolchain, tarball sig/SHA-256
verify, sources, patch, build, install into `deps/libunistring-<target>/`).
Notes that cost real time (both frozen 1.4.2):

- **GNU `.sig` needs `gpgv`, which is NOT on the host.** The arcana above said
  "gpg isn't on this box, gpgv is" — true only *inside the amd64 container*
  where the darwin8 build ran. On the host the Retro68 build hits `gpgv: command
  not found`, so the script falls back to a **pinned SHA-256**
  (`5b46e74377ed7409c5b75e7a96f95377b095623b689d8522620927964a41499c` for
  `libunistring-1.4.2.tar.xz`, computed from the GNU-sig-verified download).
- **`socklen_t` hard error (Retro68 only).** Classic Mac has Open Transport, no
  BSD sockets, so gnulib's `gl_TYPE_SOCKLEN_T` can't find an equivalent and
  aborts: `error: Cannot find a type to use in place of socklen_t`. The socket
  modules are only *indicators* (no socket `lib/*.c` is built — 86 `.c`, all
  Unicode), so a cache override is safe: `gl_cv_socklen_t_equiv=int` →
  `config.h` `#define socklen_t int`. darwin8/Tiger *does* have `<sys/socket.h>`,
  so it finds a real `socklen_t` and needs no override.
- **`AVOID_ANY_THREADS` on BOTH tiers** (the darwin8 fix above applies verbatim to
  Retro68): configure sets `PTHREAD_CREATE_IS_INLINE=1` (pthread_create is an
  inline on the classic SDKs) → `gl_pthread_api=no` → `mbtowc-lock.h` matches NO
  branch → `mbtowc_with_lock` undefined and `mbrtowc.c`/`mbrtoc32.c` fail.
  `#define AVOID_ANY_THREADS 1` in the generated `config.h` routes it to the
  no-lock branch.
- **`getlocalename_l-unsafe.c` `#error` (Retro68 only).** gnulib's
  `localename` module ends in `#error "Please port gnulib getlocalename_l-unsafe.c
  to your platform!"` for unported OSes. darwin8 defines `__APPLE__ && __MACH__`
  so it takes gnulib's Mac OS X branch and never reaches it; Retro68's
  `powerpc-apple-macos-gcc` defines only `__PPC__`/`__powerpc__`, so it lands on
  the `#error`. Might look fatal but is dead-on-arrival code (classic Mac has no
  per-locale names). Patch: add a `#elif defined __PPC__ || defined __powerpc__
  || defined __MACH__` branch returning `{ "C", STORAGE_INDEFINITE }` (the
  script's perl one-liner).
- **No `iconv` on Retro68 libc** (verified: `iconv_open`/`iconv_close` don't
  LINK). configure sees `iconv.h` and defaults `DEPENDS_ON_LIBICONV=1`, so
  `uniconv`'s `u8_conv_*` objects reference iconv and the M-tier the_Foundation
  link would die with undefined `.iconv_open`. With T-tier, `osx/CMakeLists.txt`
  sets `UNISTRING_ICONV=NO` (iconv comes from libSystem) and that's correct. For
  Retro68, configure with `ac_cv_header_iconv_h=no am_cv_func_iconv=no` (the
  script does) → the archive is self-contained (nm shows NO `U iconv*`). The
  `u8_conv_*` paths then run iconv-free (a UTF-8-only workable path); the M-tier's
  own non-UTF-8 encoding conversion is a separate open item.
- **GNU sed warning is benign**: during `make`, libunistring's `declared.sh`
  complains "The 'sed' program is not GNU sed" (macOS BSD sed) and then
  "Continuing with existing libunistring.sym." — the symbol list is already
  correct; the library builds and installs fine. Don't chase it.

### the_Foundation classic C libs — PCRE2 + zlib (+ HarfBuzz/FriBidi)

The mandatory GNU C libs the_Foundation links on the M-tier are cross-built for
Retro68 via `scripts/setup-mtier-libs.sh` (→ `deps/pcre2-retro68`,
`deps/zlib-retro68`); the text-shaping libs via
`scripts/setup-harfbuzz-fribidi.sh` (→ `deps/harfbuzz-retro68`,
`deps/fribidi-retro68`). mbedTLS-ppc is de-scoped here (proven in starscape on
OS 9 via ClassicNet — do not re-litigate).

- **PCRE2 10.47**. `regexp.c` (`iRegExp`) is required by `gmdocument.c`.
  Cross-build (autotools, out-of-tree, `--host=powerpc-apple-macos
  --disable-shared --enable-static --enable-unicode`) hits TWO Retro68 gotchas:
  * **`int32_t` is `long` on Retro68's stdint.h (PPC32), not `int`** — so the
    ubiquitous `int32_t *` vs `int *` pointer mix in `pcre2_compile.c`/callers
    is a TYPE mismatch the compiler *errors* on. Both are 32-bit on PPC32, so
    it's ABI-safe — just downgrade: build with
    `CFLAGS="-O2 -Wno-error=incompatible-pointer-types -Wno-incompatible-pointer-types"`.
    Do NOT patch the source (the darwin8 build needs `int32_t==int`).
  * **`make install` compiles `pcre2grep`, which `#include <io.h>` (Windows)** and
    dies. Install the library + headers only: build `libpcre2-8.la
    libpcre2-posix.la`, then `cp src/pcre2.h` (generated in the *build* dir) and
    `$SRC/src/pcre2posix.h` (source dir — NOT generated) + `.libs/*.a` into the
    prefix. (The POSIX wrappers are exported as `pcre2_regcomp`/`pcre2_regexec`/
    `pcre2_regerror`/`pcre2_regfree`, not plain `reg*`.)
- **zlib 1.3.1**. `archive.c` inflates `deflated_Compression=8` ZIP entries —
  `resources.lgr` (the app's bundled fonts/about pages) is deflated, so this is
  REQUIRED for the M-tier app to boot, not just an optional `iHaveZlib`.
  Gotcha: **zlib's macOS configure sets `AR=libtool -o`**, and host `libtool`
  silently drops the non-Mach-O Retro68 objects (warnings "not a mach-o") and
  leaves a ~96-byte **empty** `libz.a`. Archive with the Retro68 ar:
  `make libz.a AR=powerpc-apple-macos-ar ARFLAGS=rc` (and skip
  `example`/`minigzip`, which don't cross-link). Install `zlib.h`/`zconf.h` +
  `libz.a` manually.
- **HarfBuzz + FriBidi are now BUILT for Classic** (`scripts/setup-harfbuzz-fribidi.sh`
  → `deps/harfbuzz-retro68`, `deps/fribidi-retro68`) — the original decision to
  drop them was reversed (the user wants the shaped rendering). Both are
  deps-free because lagrange uses HarfBuzz's **default font funcs**
  (`hb_blob_create` → `hb_face_create` → `hb_font_create` in `fontpack.c`) and
  rasterizes glyphs itself via stb_truetype — **no FreeType/glib/icu/cairo**.
  Gotchas (each cost real time):
  * **HarfBuzz 2.8.2 is meson-ONLY** (autotools `configure` was removed; only
    `meson.build`/`CMakeLists.txt`/stale `Makefile.am` remain). Cross-build with a
    meson cross-file (`retro68.meson-cross.txt`: `powerpc-apple-macos-gcc/-g++`,
    `system=darwin`, `cpu_family=ppc`, `endian=big`) and
    `-Dglib/-Dgobject/-Dfreetype/-Dicu/-Dcairo/... disabled`. **The valid 2.8.2
    option names differ** from newer releases: `graphite` (not `graphite2`),
    `icu` (+`icu_builtin`), `coretext`, and there is **no** `fontconfig`/`brotli`
    option — passing those errors "Unknown option".
  * **`HB_NO_MT` is mandatory**: Retro68 has `pthread_mutex_t` (so HarfBuzz's
    hb-mutex.hh picks the pthread branch) but NOT the `pthread_mutex_*`
    functions — same classic-Mac thread gap as libunistring's
    `PTHREAD_CREATE_IS_INLINE`. `-Dcpp_args=-DHB_NO_MT` routes hb-mutex.hh to
    the no-op lock (single-threaded target; glyph output is unchanged).
  * **`-Wno-format`**: `hb_codepoint_t`/`uint32_t` is `unsigned long` on Retro68,
    so `%u` in hb-font.hh/hb-aat-layout-* is a TYPE-, not ABI-, mismatch (both
    32-bit) — harmless, but silence it or a `-Werror` build dies.
  * **FriBidi's CLI tool fails**: `bin/fribidi-main.c` `#define false (0)` trips
    on Retro68 — build+install only the library (`make -C lib && make -C lib
    install`).
  * The M-tier renderer must define `LAGRANGE_ENABLE_HARFBUZZ=1` +
    `LAGRANGE_ENABLE_FRIBIDI=1` and link `libharfbuzz.a`/`libfribidi.a` (they
    belong to the *app/renderer* target, not the_Foundation).
  Other optional codecs (WebP/JXL/mpg123/opus/Sparkle) are all `*_FOUND`-gated
  and compile out — M-tier feature trim per the plan, no build.

### the_Foundation on Retro68 (classic Mac) — feasibility + the real port surface

The L9 ("Classic lagrange") next slice is the_Foundation + ClassicNet OT seam
cross-built for OS 8/9 (the mirror of the darwin8 `d8_tls_smoke`). This is a
genuine PORT, not the darwin8-style 4-shim affair — darwin8 needed a few
POSIX shims because Tiger is still POSIX; classic Mac (Retro68) is not.

**Feasibility (verified 2026-09-12):** it DOES configure and is largely
compilable, because Retro68 ships a real POSIX-compat subset unlike porters
expect.  Confirmed in `$RETRO68_ROOT/powerpc-apple-macos/include/`:

- **Threads WORK, fully.** Retro68 provides `pthread.h` + `libThreadsLib.a`
  (backed by the classic Threads manager) WITH the exact functions the_Foundation's
  `thread.c`/`mutex.c`/condition-variables need: `pthread_create/join/detach/self`,
  `pthread_mutex_*`, `pthread_cond_*` (incl. `timedwait`/`clockwait`),
  `pthread_key_*` (tss), `pthread_setname_np` (the 1-arg form thread.c uses), and
  `pthread_setcanceltype`.  So `iHavePThread` detects truthfully and the seam's
  `iTlsRequest` worker thread does NOT need a fallback.  (This is why the L9
  `cn_ot_smoke` already links `ThreadsLib` for `YieldToAnyThread()`.)
- **Present** (compiles as-is): `unistd.h`, `sys/stat.h`, `sys/types.h`,
  `sys/time.h`, `fcntl.h`, `sys/wait.h`, `sys/select.h`, `errno.h`, `stdint.h`.
- **MISSING** (`sys/socket.h`, `netdb.h`, `poll.h`, `dlfcn.h`) — these are the
  real gaps; the socket/address/datagram/service classes are where the port
  lives.  `dirent.h` exists but is a stub that `#error`s ("not supported");
  `sys/dirent.h` errors too.

**Configure recipe (proven):** point the Retro68 toolchain +
`TFDN_CLASSICNET=ON TFDN_ENABLE_TLSREQUEST=ON TFDN_STATIC_LIBRARY=ON` +
`TFDN_ENABLE_{TESTS,WEBREQUEST,WARN_ERROR,SSE41,DEBUG_OUTPUT,MUTEX_DEBUG}=OFF`,
`UNISTRING_DIR=<abs deps/libunistring-retro68>`, `UNISTRING_ICONV=NO`,
`PCRE2_ROOT`/`ZLIB_ROOT` = the Retro68 static dep trees.  **⚠️ The fork MUST
absolutize those roots and MUST NOT let host pkg-config resolve the deps** —
on this box homebrew pkg-config leaks the x86_64 `pcre2`/`zlib` into the PPC
cross-build (wrong arch at link).  The fork's `Depends.cmake` RetroPPC branch
sets `*_FOUND`/dirs/lib directly from the roots and forces `CURL_FOUND=NO`,
`OPENSSL_FOUND=NO` (TLS is the ClassicNet seam).  Omitting a root leaves the
dep safely OFF (`iHaveRegExp/iHaveZlib` unset), which is fine for a seam-only
smoke (the darwin8 `d8_tls_smoke` never touched PCRE2/zlib either).

**Fork platform classification (landed 2026-09-12, `classicnet-seam`):**
`CMAKE_SYSTEM_NAME STREQUAL "RetroPPC"` → `iPlatformClassic` (new `config.h.in`
`#cmakedefine`), platform file `generic.c`; the whole POSIX platform layer
(`posix/{datagram,locale,pipe,process,service}.c` — BSD socks / dlopen /
posix_spawn) and `posix/socket.c` are **not** built when `iPlatformClassic`; the
network seam uses `platform/classicnet/socket.c` + `tlsrequest.c`. Non-classic
tiers are untouched (verified: the host `iPlatformApple` the_Foundation still
configures+builds clean, producing `lib_Foundation.a`).

**Defect inventory — the actual port (each is a per-file job, 2026-09-12):**
- `src/address.c` — depth-1: `#include <sys/socket.h> <netdb.h> <ifaddrs.h>`
  and the `getaddrinfo`/`struct addrinfo` resolver body + `sockaddr` are
  unconditionally POSIX.  The ClassicNet seam DOES use `iAddress`
  (`tlsrequest.c` calls `lookupTcpCStr_Address` + `waitForFinished_Address`),
  so it can't be dropped; it needs a Classic branch that (a) gated the socket
  header includes and (b) resolves via the CNTransport host/port instead of
  `getaddrinfo` (the actual DNS/connect happens inside `CN_OTCreate`), or a
  raw-sockaddr-free stub that still completes `lookupFinished`.
- `src/fileinfo.c` — `iFileInfo` uses `struct stat` (`st_mtimespec` — not on
  classic's stat) and `<dirent.h>` (not supported on Retro68).  Needs a Classic
  `FSOpen`/`FSMakeFSSpec`/`FInfo`-based implementation for type/size/time +
  directory enumeration.  **Not needed by a seam-only smoke** → can be excluded
  from the Classic SOURCES for the first slice (the darwin8 seam-only smoke also
  skipped PCRE2/zlib/archive).
- `src/block.c` — `crc32_Block`: on Retro68 `uint32_t` is `unsigned long` while
  `iCrc32`/`unistring_uint32_t` is `unsigned int`; the definition's return type
  conflicts with `block.h`'s `uint32_t` prototype.  A real Retro68 `uint32_t`
  type-width fix (same class of bug as the PCRE2 `int32_t==long` mismatch, but
  here it needs a source-side resolution, not just a warning suppression).
- Likely once those pass: `path.c`/`file.c`/`networkproxy.c`/`threadpool.c`
  POSIX assumptions; `src/platform/generic.c` may need a classic file/runtime
  backend.
- **The seam itself is darwin8-only today**: `platform/classicnet/socket.c` and
  `tlsrequest.c` call `CN_Darwin8Create` (and assume `getaddrinfo` inside it).
  For classic it must call the OT create instead (`CN_OTCreate`/the
  `cn_ot` CNTransport vtable the L9 `cn_ot_smoke` already proves).  The poll /
  send / recv / close vtable is transport-agnostic, so this is a
  `CN_WITH_DARWIN8` vs `CN_WITH_OT` branch around the create, not a rewrite.

**The seam-only slice stop:** the clean, committable increment landed on
`classicnet-seam` is the platform classification + dep wiring + inventory above.
The working `d8_tls_smoke`-for-classic (iTlsRequest over OT, cross-built and
fetching on the macos9 guest via a boot-root log) is the next slice; it needs the
`address.c`/`block.c` Classic fixes, the OT create branch in the seam, a
`mac/CMakeLists.txt` target linking the_Foundation + `cn_ot` + mbedTLS-ppc, and
a Retro68 `tls_smoke`.

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
   `NSSetUncaughtExceptionHandler` + app-owned logfile (`L4.log`
  discipline: bare execs over ssh can't even reach WindowServer).
- Autoresize masks are edge-pinning, not flexing: bottom-pinned view =
  `NSViewMaxYMargin` (flex the TOP), and a missing width mask silently
  kills all resize reflow.
- Deploy: ship `.app` bundles via scp (Tiger's tar rejects gzipped
  bundles); `killall` old instances by name first (Tiger instance
  stacking); `screencapture` has no `-R` (full-screen + host-side crop).

lagrange's Aqua canvas host (`src/macos/aquaview.m`, cross-build DONE 2026-09-10)
adds a few more:

- **CarbonCore ships its own `resources.h`.** Do NOT do a project-wide
  `include_directories(${MACCORE})` — it shadows lagrange's `src/resources.h`
  for the widget kit (`window.c`'s `#include "resources.h"` grabs CarbonCore's
  and `imageShadow_Resources`/`imageLogo_Resources` go undeclared). Attach
  MACCORE per-target (`cn_d8`, `the_Foundation`) as PUBLIC usage requirements so
  it lands AFTER the app's own `src/` include path.
- **`canvasmenu_impl_SDL.m` is modern-AppKit only** — generics
  (`NSMutableDictionary<...>`), `NSApplicationActivationPolicyRegular` (10.6+),
  `NSEventModifierFlags` (10.6+), `popUpMenuPositioningItem:...` (10.7+). Not
  usable on 10.4. The host uses the portable null `canvasmenu.c` (widget kit
  draws its in-window menubar) until a `canvasmenu_impl_aqua.m` is written.
- **10.4 has no `NSWindowDelegate`/`NSApplicationDelegate` formal protocols**
  (delegate methods are informal) — don't declare `<NSWindowDelegate>`
  conformance; just `@interface X : NSObject` + implement `windowWillClose:`.
- **`[NSEvent pressedMouseButtons]` is 10.6+** — track the button bitmask in a
  view ivar instead (set in `mouseDown:`/`mouseUp:`).
- **`NSCursor` resize cursors (`resizeUpDownCursor`/`resizeLeftRightCursor`)
  are 10.6+** — guard with `[NSCursor respondsToSelector:]`.
- **ObjC needs `-std=gnu99` + `-fobjc-exceptions`** (`set(CMAKE_OBJC_FLAGS
  "...-std=gnu99 -fobjc-exceptions")`); gnu89 rejects C99 for-loops in `.m`.
- **Several widget-kit blocks are `#if !defined(NDEBUG)`** (renderer-info
  texture formats, the `SDLK_KP_1` debug theme-seed shortcut) — build the Aqua
  target with `NDEBUG` like the host canvas Release gate.
- **10.4u SDK has no `<execinfo.h>`** (`sdlcompat.c` backtrace path) — shim it
  (`osx/darwin8_sdk_shim/execinfo.h`, frame-pointer walk over r30).
- **Multi-window: present/created/destroyed/title hooks key by `SDL_WindowID`,
  NEVER by array index (2026-09-11).** `sdlcompat.c`'s `g_windows[]` includes
  the widget kit's popup/menu windows (`newPopup_Window` → `SDL_CreateWindow`)
  and `SDL_DestroyWindow` swaps-last-into-place on remove — so an index is
  unstable.  **And `SDL_CreateRenderer` must set `renderer->window = win`** —
  it never did, so `SDL_RenderPresent`'s `r->window->id` was `0` and every
  present landed on the backend's id lookup at `0` → −1 → no blit → a black
  window (chrome + title still render via the title hook, hiding the failure).
  The present hook int arg, `setWindowDestroyedHook_` and `setWindowTitleHook_`
  therefore carry the presenting/affected window's
  *windowID*; `canvasPixelsByWindowId_canvas` resolves pixels by id.  The Aqua
  host keys its native-window table by id (`gAqTable`).  **Popup/dropdown
  windows carry no native window** — the Aqua host skips
  `SDL_WINDOW_POPUP_MENU`/`SDL_WINDOW_SKIP_TASKBAR` windows (this host renders
  the top/context menus natively via `canvasmenu_impl_aqua.m`, so a widget-kit
  dropdown is in-canvas and must not become a separate floating NSWindow).
- **Native window close ≠ app quit once there are extra windows.** The Aqua
  host kept "close the window → SDL_QUIT + `[NSApp terminate:]`".  With extra
  windows that's wrong: clicking an *extra* window's title-bar button should
  just close it.  `windowWillClose:` now: primary (first native window,
  `gAqTable[0]`) → quit; any other → `SDL_WINDOWEVENT_CLOSE` so the widget kit
   runs `closeWindow_App` → `SDL_DestroyWindow` → the destroy hook tears the
   NSWindow down.  The backend holds a +1 on each NSWindow from creation and
   releases exactly once in the destroy hook.  **Do NOT call `[NSWindow close]`
   from the destroy hook — `close:` fires the `windowWillClose:` delegate
   synchronously**, which re-enters the AquaAppDelegate; for the primary window
   that calls `quitRequested` → `[NSApp terminate:]` from right inside the
   timer-tick frame that is tearing down the window model → a double release
   (`EXC_BAD_ACCESS` in `objc_msgSend`, `L4.crash.log`, `delete_MainWindow` →
   `windowDestroyedAqua_`).  Use `[NSWindow orderOut:]` (removes it from screen,
   no delegate notification) + the one balanced `release`; clear the freed table
   slot so a stale id lookup can't re-hit a released object.  **2026-09-11,
   found + fixed on-device.**
- **Every native window the backend creates must be `[setReleasedWhenClosed:NO]`
  (2026-09-12).** With the default YES, closing a window *via its title-bar close
  button* makes AppKit deallocate it; the shim's destroy hook then runs later (on
  the next tick, when the queued `SDL_WINDOWEVENT_CLOSE` is processed) and
  messages the freed NSWindow/NSView — `EXC_BAD_ACCESS` in
  `windowDestroyedAqua_` (`delete_Window` → shim destroy path).  A backend-held
  +1 is NOT sufficient to keep it alive here.  `releasedWhenClosed:NO` holds the
  window until the destroy hook's one balanced `release`.  This is why the
  detached **Preferences** window (promoted to an extra window via
  `LAGRANGE_AQUA` + `promoteDialogToWindow_Widget`) crashed when the user closed
  it.  **2026-09-12.**
- **`\uXXXX` escapes inside Objective-C `@""` literals are garbled by the
   Retro68/10.4u GCC toolchain** (mojibake for a non-ASCII glyph).  To embed a
   non-ASCII char in a string, build it UTF-8-aware: `<NSString
   stringWithUTF8String:">` with the raw bytes, e.g. coffee U+2615
    `"Powered by Aqua, mbedTLS, and \xE2\x98\x95"`.  **2026-09-12**, About dialog.
- **Tiger `.icns` must contain ONLY PNG entries `ic07`/`ic08`/`ic09`** (128/256/512).
  `iconutil` emits `ic12`/`ic13`/`icp4`/`icp5`/`icp6`, any of which makes Tiger reject
  the whole file; a hand-built `ic04`/`ic05`/`8mask` classic bitmap is also unreadable
  (`8mask` is even a 5-byte type -- invalid).  A PNG-only `icns` (no classic entries)
  decodes on Tiger (`NSImage` → `reps=3`).  Build recipe: resize `res/lagrange-256.png`
  → 128/256/512 PNGs, wrap them as `icns`+`[type,len,data]`.  All lagrange icon sources
  now have `res/*.icns` this way.
- **Tiger's `orderFrontStandardAboutPanel` NEVER shows the app icon** even with a
  decodeable, LaunchServices-registered `.icns` (`/Applications` + `lsregister -f`).
  It resolves the icon from the registered bundle, and an unregistered/GUI-bundle
  path yields none; `[NSApp setApplicationIconImage:]` drives the **dock** but is
  ignored by the About panel.  The lagrange About panel is therefore kept native
  WITHOUT an icon (2026-09-12); the dock icon works via `setApplicationIconImage:`
  (built from the bundled `lagrange-64.png` with stb_image → `NSBitmapImageRep`).
- **The canvas host is multi-window now, so `app.c` gates `detachedPrefs` on
  `LAGRANGE_AQUA`** (added to the osx L4 `target_compile_definitions`): a
  detached Preferences window is visible + focusable.  The other `LAGRANGE_CANVAS`
  hosts (sdlview/headless) still keep it in-window, because a detached window
  there would be invisible (single-framebuffer present) → the app appears frozen.

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

### M-tier network-first slice (`cn_ot_smoke`, 2026-09-12)

The Retro68 cross-build of the ClassicNet OT slice (`cn_ot` + `cn_tls` +
`cn_mac_time` + mbedTLS-ppc) into a `cn_ot_smoke` console .bin is the
M-tier analog of the T-tier `d8_smoke`. Facts that cost a round:

- **`vendor/ClassicNet/scripts/setup-mbedtls.sh` has a bash line-continuation
  bug in the PPC step** — a trailing `# comment` after a `\` breaks the
  chain, so the `cmake --build` never runs (shell prints `command not
  found`) after a *successful configure*. The workaround is configuring with
  the full flag set and running `cmake --build` manually (mac/CMakeLists.txt
  has the exact invocation). The same script's TCP comparison is the one to
  fix upstream. Do not trust `-DENABLE_TESTING=Off` to survive.
- **`MBEDTLS_PPC_ROOT` is the mbedTLS SOURCE root, not the build dir** — the
  include dir is `<root>/include` and the libs are
  `<root>/build-ppc/library/*.a`. Pinning these separately is easy to get
  backwards on a first port.
- **`-I` for the mbedTLS user config must point at `vendor/ClassicNet/target`
  (not the mbedTLS tree)** or every TU fails `mbedtls_userconfig.h: No such
  file` — 3rdparty/everest + p256-m build first and are the first to trip.
- **LaunchAPPL's socket-returned stdout does NOT stream for CONSOLE binaries
  on the macos9 guest that we can reach** — `LaunchAPPL --emulator tcp`
  transfers the whole file (port 1984 accepts) then the host client
  SIGPIPEs (exit 141) with no app stdout, for our `cn_ot_smoke.bin` and a
  known-good starscape `cntest.bin` alike. The repo's `vm/logs/*` evidence
  for this guest is entirely screenshots (`qemu-shot.sh`), so the
  "LaunchAPPL captures stdout" claim in starscape's `test-on-device.sh` has
  never produced a captured log here. Treat LaunchAPPL-over-darwin8-tcp as
  unproven on this machine.
- **To run ANY command-line app on the macos9 guest for evidence, do NOT
  rely on LaunchAPPLServer** — it is not in the base clone or the bare
  `macos9.qcow2` (only ever in a disposable clone starscape threw away),
  and provisioning it is a one-time interactive flow
  (`vendor/ClassicNet/scripts/build-launchappl-iso.sh` → StuffIt-expand →
  "OpenTransport TCP" :1984 → Startup Items). The reliable path is
  **Startup Items + a boot-root log + retrieve-log.sh** (see below).
- **A CONSOLE app CAN auto-launch from Startup Items — but only with a real
  creator.** `add_application(... CONSOLE ...)` with NO `CREATOR` stamps
  creator `????`; deployed as a Startup Item, Finder refuses to launch it
  with error **-199** ("could not be opened"). Set `CREATOR "CnOs"` (any
  real 4-char) and the same app launches fine. The `.bin` MacBinary header
  and the `.APPL` AppleDouble both carry whatever `CREATOR` the build set,
  so the fix is purely a build flag — deploy via the `.APPL` + `%name.ad`
  Startup-Items path (starscape's `deploy-qemu.sh`) with that creator.
- **Console apps need a bigger memory partition than the 1 MB toolchain
  template** — without a `resource 'SIZE' (-1)` override the guest launches
  with "not enough memory available". Ship a `cn_ot_smoke.r` SIZE block
  (mirror starscape's `guest_suite.r` / `gemini.r.in`: prefer 32 MB / min
  16 MB) and list it in the `add_application` FILES.
- `smoke_tee` logging to a boot-root file (`FSMakeFSSpec(0,0,...)` +
  `FSpCreate`/`FSpOpenDF`/`FSWrite`) is the right *capture* design: the app
  writes `cn_ot_smoke.log`, the VM is stopped, and
  `~/classic/vm/bin/retrieve-log.sh --vm macos9 cn_ot_smoke.log` pulls it.
  It only works once the app actually launches (real creator + SIZE).
- **Deploy clones are disposable and the base clone's Startup Items are
  empty** — `deploy-qemu.sh` clones `macos9_base.raw`, which carries no
  Startup-Items apps, overwriting any prior `macos9_test.raw`. If a
  previous session left a provisioned guest in `macos9_test.raw`, cloning
  fresh silently destroys it. Keep a provisioned guest as a separate named
  disk if you want it to survive.

## Loop / evidence (inherited discipline)

- **See ClassicNet live: `[classicnet]` stderr markers.** ClassicNet logs one
  line per transport to stderr — `[classicnet] TCP connect <host>:<port>
  (cn_darwin8)` and `[classicnet] TLS handshake OK (mbedTLS via cn_tls)`. This
  is the fastest ground-truth that a fetch went through the seam rather than
  stock OpenSSL/posix: run the binary and watch. (Confirmed against `canvaswin`
  over the seam: localhost + an external `gemini://gemini.circumlunar.space:1965`
  fetch both logged the markers. The isolated-run log is `/tmp/kilo/cn.log`.)
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

## Phase 1 seam (N2) — ClassicNet-backed iSocket in the_Foundation

- **The seam is inside the_Foundation.** `CMakeLists.txt` selects `src/platform/
  posix/socket.c` (and `src/tlsrequest.c`) for the stock build; `TFDN_CLASSICNET=ON`
  swaps in `src/platform/classicnet/socket.c`. The `classicnet` lib + its PUBLIC
  usage requirements (`CN_HOST`/`CN_WITH_DARWIN8` defines, the `include` dir) must
  be attached from the *consuming* build (lagrange's `cmake/ClassicNet.cmake`)
  *after* the `classicnet` target exists, because `the_Foundation` is
  `add_subdirectory`'d before it. lagrange turns the flag on in
  `cmake/Depends.cmake` (before `add_subdirectory(the_Foundation)`).
- **ClassicNet's backend needs no iAsync Address / network-proxy machinery**:
  `CN_Darwin8Create` does the (blocking) `getaddrinfo` and the non-blocking
  connect internally, so a single background thread can do connect + the I/O
  pump. `address_Socket` is only informational for the plain-socket case;
  `gmrequest` reads the host from `address_TlsRequest`, not the plain socket.
- **`iBlock` is NOT a refcounted `iObject`** (it has its own `refCount`, not the
  object/mutex system), so a partially-sent block must be freed with
  `delete_Block(b)` — calling `iReleasePtr(&b)`/`deref_Object` treats the block's
  first bytes as an object header and asserts on `__sig` (this cost a debug
  cycle). Buffers, by contrast, *are* objects (`iReleasePtr` is correct there).
- **Non-blocking send retains the unsent remainder** across pump iterations
  (a `sending` block + `sendOff` member); `mbedtls`-style WANT/WOULD-block frees
  nothing until the whole block is out. The pump sleeps on `CN_Darwin8Wait` with
  a short timeout so queued output flushes without a separate wakeup pipe.
- `new_Socket` is generated by `iDefineObjectConstructionArgs(Socket,
  (const char *, uint16_t), host, port)`; a backend that omits it links a test
  with `_new_Socket` undefined.

## Phase 1 seam (N2 step 2) — the `TlsRequest` backend is monolithic

- **`src/tlsrequest.c` is one file defining BOTH `iTlsCertificate` and
  `iTlsRequest`.** `gmcerts.c` exercises almost every `iTlsCertificate` method
  (subject/issuer *name components*, `subjectAltNames`, fingerprints,
  `verify`/`verifyDomain`, `validUntil`/`isExpired`, `pem`, `equal`,
  `newSelfSignedRSA_TlsCertificate`). Since swapping `tlsrequest.c` out for the
  ClassicNet backend removes all of them, a partial port would leave undefined
  symbols at link time — the mbedTLS replacement must export the full API.
- **mbedTLS has no single-call OpenSSL-style cert generator, but it CAN mint a
  self-signed cert** with `mbedtls_x509write_crt` + `mbedtls_rsa_gen_key`
  (`MBEDTLS_X509_CRT_WRITE_C`+`MBEDTLS_RSA_C`+`MBEDTLS_GENPRIME`). Gotchas:
  * `mbedtls_x509write_crt_der` serializes the DER *backwards* into the buffer
    and returns the length — the cert is at `buf + size - len`, not `buf`.
    (`sizeof(der)` on a `malloc`'d pointer is the pointer width; use the buffer
    byte count.) A parse-from-the-wrong-offset fails with
    `MBEDTLS_ERR_X509_UNKNOWN_VERSION`.
  * `mbedtls_x509write_crt_set_version(ctx, MBEDTLS_X509_CRT_VERSION_3)` — the
    constant is `2`, not the literal `3` (that writes an invalid v4/unknown
    version).
  * mbedTLS's name parser (`mbedtls_x509_string_to_names`) recognises CN/C/O/L/
    OU/ST/emailAddress/DC/... but **not** the literal `UID` encountered in
    lagrange's identity names. Encode UID with its numeric OID plus a DER-hex
    value: `0.9.2342.19200300.100.1.1=#0c<len><utf8bytes>` (the `#hexDER` value
    form).
  * The `d->cert` in the mbedTLS verify callback already owns the offending leaf;
    a rejection should just set `certVerifyFailed` (a `certificateVerifyFailed`
    that deletes `d->cert` then `copy_TlsCertificate(same cert)` is a
    use-after-free — the callback's cert aliases `d->cert`).
- **The `iTlsCertificate` CA store is a global that must be reset, not appended**
  — `mbedtls_x509_crt_parse` *appends* to the chain, so calling `setCACertificates`
  with an empty bundle (TOFU path) must `mbedtls_x509_crt_free`+`init` the store
  first, or the trust anchor leaks across requests and a no-CA fetch still
  reports `authority`.
- **App (GUI) builds must set `-DCN_SANITIZE=OFF`.** ClassicNet compiles its host
  lib with ASan/UBSan by default (`CN_SANITIZE`, ON when `CN_HOST`), and the
  ClassicNet test executables carry their own sanitizer link flags — but the
  plain `canvasapp`/`canvaswin` app targets do not link the sanitizer runtime, so
  pulling an ASan-instrumented `libclassicnet.a` into them fails at link on
  `___asan_init`. Only the classicnet *test* build dir (`build-classicnet`) keeps
  sanitizers on.
- **Splitting the port:** landed (2a) the `cn_tls` transport + `iTlsCertificate`
  X.509 wrapper, then (2b) the self-signed generator above. The `-P-3`/`C-3`
  naming in the plan tracks the client-cert identity work.

## Dead ends (proven — do not retry) **[starscape]**

- Secure Transport on Tiger/Classic: TLS 1.0 max — double dead for gemini.
- curl as gemini transport: no scheme support upstream, TOFU shoehorning.
- `.dsk` flat image as QEMU OS 9 boot volume: "Initialize" only.
- Bare exec over ssh reaching WindowServer on Tiger: dead end
  (`CFMessagePort bootstrap_register failed 1100`); Finder `.app` only.
- clang/LLVM for PPC32 Darwin: dropped upstream.
- `attempt_4`-style exotic Rez SIZE syntax: crashes Rez.
