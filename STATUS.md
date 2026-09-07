# STATUS

## Where we are

Fork created, PLAN.md + AGENTS.md written. Local (host, arm64 macOS) build
of stock lagrange is the next task ("step -1") before the canvas-host seam
refactor (PLAN.md phase 0).

## Last completed milestone

None yet on the port (upstream baseline = 1e46ff6f, iOS 1.21).

## In-flight

- **Host build ("step -1")**: init submodules (the_Foundation, fribidi,
  harfbuzz done; sealcurses skipped — not needed for the GUI target),
  obtain SDL2 (homebrew), configure `build-host/` with CMake, build and
  launch. No known roadblock.
