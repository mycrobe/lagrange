#!/usr/bin/env bash
#
# Cross-build the Tiger (10.4 PPC) flavor's *network-first* slice (the
# T-tier Phase 2 enabling build) inside the amd64 container -- the darwin8
# cross compiler only runs there (toolchain layout + container rules:
# ~/.local/share/doc/darwin8-toolchain.md).
#
#   scripts/build-osx.sh          -> build-osx/d8_smoke (a PPC Mach-O)
#
# d8_smoke is a CN-level Gemini fetch over cn_darwin8 + cn_tls + mbedTLS-d8:
# the proof that the vendored ClassicNet darwin8 slice is portable to Tiger.
# The full Aqua canvas host (src/macos/) and the the_Foundation seam are the
# next T-tier slices.
#
# Later slices build a bundle (L4.app) with POST_BUILD assembly, but the
# d8_smoke tool is a bare CLI executable that ships via scp (no .app bundle
# rule -- that's only for GUI apps that need the WindowServer).
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
TC="${DARWIN8_TOOLCHAIN:-$HOME/classic/darwin8-toolchain}"

# The cross-built mbedTLS-d8 this slice links.  Provision with the vendored
# ClassicNet script's darwin8 step (the same recipe that produced the host
# mbedtls-host3 and the OS 9 mbedtls-ppc).
MBEDTLS_D8="$ROOT/vendor/ClassicNet/deps/mbedtls-darwin8"
if [ ! -f "$MBEDTLS_D8/library/libmbedtls.a" ]; then
    echo "!! mbedTLS (darwin8) missing at $MBEDTLS_D8" >&2
    echo "   run: vendor/ClassicNet/scripts/setup-mbedtls.sh  (the darwin8 step)" >&2
    exit 1
fi

# PCRE2 (darwin8), a mandatory the_Foundation dep for the app's regexp support
# (gmdocument.c uses iRegExp).  Cross-build the 10.47 tarball the same way as
# libunistring: signature-verified, install into deps/pcre2-darwin8.  The
# generated .pc lives in <prefix>/lib/pkgconfig.
PCRE2_D8="$ROOT/vendor/ClassicNet/deps/pcre2-darwin8"
if [ ! -f "$PCRE2_D8/lib/libpcre2-8.a" ]; then
    echo "!! PCRE2 (darwin8) missing at $PCRE2_D8" >&2
    echo "   cross-build the pcre2-10.47 tarball (signature-verified) into" >&2
    echo "   deps/pcre2-darwin8; see docs/arcana.md 'the_Foundation on darwin8'." >&2
    exit 1
fi

# Binaries land in build-osx/ (gitignored), user-owned: Docker Desktop maps
# container-root bind-mount writes to the host user, so the container may
# run as root (apt-get needs it).
docker run --rm --platform linux/amd64 \
    -v "$TC/gcc-install":/gcc-install:ro \
    -v "$TC/cctools-out":/out:ro \
    -v "$TC":/toolchain-root:ro \
    -v "$TC/ppc-tiger-xcompiler/sdk":/sdk:ro \
    -v "$ROOT":/work \
    -e PKG_CONFIG_PATH="/work/vendor/ClassicNet/deps/pcre2-darwin8/lib/pkgconfig:/work/osx/pkgconfig" \
    ubuntu:24.04 bash -c "
        apt-get update -qq >/dev/null && apt-get install -y -qq cmake file pkg-config >/dev/null
        export PATH=/gcc-install/bin:/out/bin:\$PATH
        cmake -S /work/osx -B /work/build-osx \
              -DCMAKE_TOOLCHAIN_FILE=/work/osx/darwin8.toolchain.cmake \
        && cmake --build /work/build-osx -j \$(nproc)
    "

file "$ROOT/build-osx/d8_smoke"
