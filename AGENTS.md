# lagrange (mycrobe fork) — porting notes

This is a fork of `skyjake/lagrange` used to port the app to classic
Mac OS (8..9.2.2 PPC, Retro68) and Tiger/Leopard OS X (10.4/10.5 PPC).
Full plan: `PLAN.md` in the repo root.

## Layout

- `lib/the_Foundation`, `lib/fribidi`, `lib/harfbuzz`, `lib/sealcurses`
  are submodules — `git submodule update --init` before first build.
  The ClassicNet + mbedTLS networking work will branch inside the
  `the_Foundation` submodule (same discipline as starscape: named branch,
  host-tested, pushed, pin bumped in the consuming commit).
- `src/macos/` (T-tier Aqua/canvas host) and `src/mac/` (M-tier Toolbox)
  are the planned thin platform layers; they stay parallel-shaped, policy
  lives in the portable core.

## Builds

- Stock SDL2 build (`build-host/`) stays green at all times — it is the
  regression gate for the canvas-host seam refactor (PLAN.md phase 0).
- New flavors get their own build dirs, mirroring starscape:
  `build-mac/` (Retro68, OS 8/9 PPC), `osx/CMakeLists.txt` style for
  darwin8 Tiger/Leopard PPC.
- Cargo-cult from starscape's proven set: `-Wl,-force_cpusubtype_ALL` on
  every darwin8 link line; configure-time 64-bit-division link probe;
  `CN_TLS_FORCE_TLS12=1`.

## Ledger + arcana (conventions from starscape)

- **`STATUS.md`** (repo root) is a handoff sheet, not a history. It holds
  exactly three things: a one-paragraph *where we are*, the **last
  completed milestone** (evidence paths + how to reproduce), and the
  **latest in-flight work** (next steps + current roadblock). Not a diary:
  history lives in commit messages/`PLAN.md`; durable lessons migrate to
  this file or `docs/arcana.md` first, then the superseded STATUS section
  is cut (never appendicized). Update it in the same change as the code
  it describes. If it has more than one dated historical section, cut it
  before adding new content.
- **`docs/arcana.md`** is the dumping ground for weird, hard-won
  platform/toolchain facts: Retro68 + darwin8 quirks, Toolbox/QD traps,
  fd-churn style mysteries, dead ends (proven — do not retry). Anything
  that cost real debugging time gets written here, machine-agnostic
  (paths/VM detail stays in user-level config per the global doc).

## Docs

- `PLAN.md` — master plan, milestones, rationale.
- `STATUS.md` — live handoff ledger (see above).
- `docs/arcana.md` — platform/toolchain arcana (see above).

## Working style

Inherited machine-wide (see `~/.config/kilo/AGENTS.md`) — host tests →
QEMU guest → petal hardware, evidence discipline per that document.
Machine/toolchain paths and VM details live in the user-level config and
`~/.local/share/doc/*`, never here.

## Multi-build workflow on the host

- **Kill every running Lagrange instance before *launching* a new Lagrange
  binary** (not a general rule for building other things). `pkill -f
  Lagrange` and any stray `canvasapp` right before `open`/run of the fresh
  build. The app shares an IPC socket per user; a new instance forwards
  its arguments to the running one and exits, so `open <new build>`
  silently talks to the *old* binary — clicks reported for a fresh build
  may be the prod install's window.
- Test builds get their own saved-state directory (user-level doc keeps
  the exact commands; nothing machine-specific here beyond the rule).
- On this era of macOS, homebrew `sdl2` is an alias for `sdl2-compat`
  (SDL3 underneath) whose mouse-event coordinate basis flips between hover
  and press on Retina — stock builds and canvas harness both link **real
  SDL2** (patched with the repo's `sdl2.26-macos-ios.diff`), matching what
  released builds bundle.
- **Dev/debug builds must not report the stock version string.** The about
  dialog (and anywhere else the version shows) of every non-release build
  describes what is being tested or developed and carries the git short
  hash of HEAD at configure time, e.g. `1.21.1-dev+phase0-shim (a1b2c3d)`.
  CMake composes this from `PROJECT_VERSION`, a per-build
  `LAGRANGE_DEV_SUFFIX`, and the git hash; set the suffix when starting a
  new build flavor so a window can always be identified to its build.
