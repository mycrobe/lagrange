# Cross toolchain: Mac OS X 10.4 Tiger (darwin8) PowerPC.
#
# Only valid INSIDE the amd64 build container driven by scripts/build-osx.sh
# -- the compiler binaries themselves are x86_64 Linux programs (danupsher
# GCC 15.2 + cctools-port), run under Rosetta.  The paths below are the
# container mount points (also the absolute paths the gcc exec-dir shims
# were built against); layout + mount rules:
# ~/.local/share/doc/darwin8-toolchain.md
#
# The gcc driver finds as/ld through its own exec-dir shims; only the
# binutils CMake calls directly (ar/ranlib/strip) need explicit paths --
# the cctools binaries carry the target-prefixed names only.
#
# Cargo-culted from starscape's proven osx/darwin8.toolchain.cmake.

set(CMAKE_SYSTEM_NAME Darwin)
set(CMAKE_SYSTEM_PROCESSOR powerpc)

set(CMAKE_C_COMPILER    /gcc-install/bin/powerpc-apple-darwin8-gcc)
set(CMAKE_OBJC_COMPILER /gcc-install/bin/powerpc-apple-darwin8-gcc)

set(CMAKE_AR     /out/bin/powerpc-apple-darwin8-ar     CACHE FILEPATH "cctools ar")
set(CMAKE_RANLIB /out/bin/powerpc-apple-darwin8-ranlib CACHE FILEPATH "cctools ranlib")
set(CMAKE_STRIP  /out/bin/powerpc-apple-darwin8-strip  CACHE FILEPATH "cctools strip")
