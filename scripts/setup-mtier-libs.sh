#!/usr/bin/env bash
#
# Cross-build the OTHER mandatory GNU the_Foundation C libraries for the
# classic-Mac M-tier (Retro68 / OS 8/9 PPC).  These sit alongside
# libunistring (scripts/setup-libunistring.sh) and mbedTLS (ClassicNet's
# setup-mbedtls.sh, PPC fork -- already proven in starscape on OS 9).
#
#   deps/pcre2-retro68   PCRE2 10.47   -- iRegExp, REQUIRED (gmdocument.c)
#       include/pcre2.h, pcre2posix.h ; lib/libpcre2-8.a, libpcre2-posix.a
#   deps/zlib-retro68    zlib 1.3.1    -- iHaveZlib, REQUIRED (resources.lgr ZIP
#       include/{zlib.h,zconf.h} ; lib/libz.a             inflate + block.c deflate)
#
# The text-shaping libs (HarfBuzz + FriBidi) are built separately by
# scripts/setup-harfbuzz-fribidi.sh -- they link into the renderer/fontpack, not
# the_Foundation.
#
# Usage: scripts/setup-mtier-libs.sh    (native host; needs the Retro68 toolchain)
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
DEPS="$ROOT/vendor/ClassicNet/deps"
# THIS machine's Retro68 (the setup-mbedtls.sh default above points elsewhere).
TC="${RETRO68_TOOLCHAIN:-$HOME/classic/Retro68-build/toolchain}"
export PATH="$TC/bin:$PATH"
JOBS="$(getconf _NPROCESSORS_ONLN 2>/dev/null || echo 4)"
for t in powerpc-apple-macos-gcc powerpc-apple-macos-ar powerpc-apple-macos-ranlib; do
    command -v "$t" >/dev/null || { echo "!! $t not found under $TC (set RETRO68_TOOLCHAIN)" >&2; exit 1; }
done
mkdir -p "$DEPS"

# cache vars common to both
host=powerpc-apple-macos

# --------------------------------------------------------------------------
# PCRE2 10.47 (autotools; out-of-tree build, lib+headers install only)
# --------------------------------------------------------------------------
PCRE2_SRC="$DEPS/pcre2-src/pcre2-10.47"
PCRE2_BUILD="$DEPS/pcre2-retro68-build"
PCRE2_PREFIX="$DEPS/pcre2-retro68"
PCRE2_TAR="$DEPS/pcre2-10.47.tar.gz"
PCRE2_SHA=c08ae2388ef333e8403e670ad70c0a11f1eed021fd88308d7e02f596fcd9dc16

fetch_verify() { # <tarball> <sha256> <url>
    if [ ! -f "$1" ]; then
        curl -fsSL --max-time 120 -o "$1" "$3"
    fi
    echo "$2  $1" | shasum -a 256 -c - >/dev/null
}

if [ ! -f "$PCRE2_PREFIX/lib/libpcre2-8.a" ]; then
    echo ">> PCRE2 10.47 (retro68) ..."
    fetch_verify "$PCRE2_TAR" "$PCRE2_SHA" \
        https://github.com/PCRE2Project/pcre2/releases/download/pcre2-10.47/pcre2-10.47.tar.gz
    rm -rf "$DEPS/pcre2-src" "$PCRE2_BUILD"; mkdir -p "$DEPS/pcre2-src" "$PCRE2_BUILD"
    tar -xzf "$PCRE2_TAR" -C "$DEPS/pcre2-src"
    ( cd "$PCRE2_BUILD"
      "$PCRE2_SRC/configure" --build="$("$PCRE2_SRC/config.guess")" --host="$host" \
          --prefix="$PCRE2_PREFIX" --disable-shared --enable-static --enable-unicode \
          >/dev/null
      # Retro68's stdint.h maps int32_t to `long` (not `int`) on PPC32, so the
      # ubiquitous int32_t*/int* pointer mix in pcre2_compile.c is a TYPE,
      # not an ABI, mismatch (both 32-bit).  Gnarl-flagged as an error by
      # Retro68 gcc; downgrade to a warning.  DO NOT touch the source.
      # Build the library objects only: `make install` would try to build
      # pcre2grep, which #includes <io.h> (Windows) and fails here.
      make -j"$JOBS" libpcre2-8.la libpcre2-posix.la \
          CFLAGS="-O2 -Wno-error=incompatible-pointer-types -Wno-incompatible-pointer-types "
      rm -rf "$PCRE2_PREFIX"; mkdir -p "$PCRE2_PREFIX/include" "$PCRE2_PREFIX/lib"
      cp src/pcre2.h "$PCRE2_PREFIX/include/"
      cp "$PCRE2_SRC/src/pcre2posix.h" "$PCRE2_PREFIX/include/"
      cp .libs/libpcre2-8.a .libs/libpcre2-posix.a "$PCRE2_PREFIX/lib/" )
else
    echo ">> PCRE2 (retro68) already built"
fi

# --------------------------------------------------------------------------
# zlib 1.3.1 (configure; build only libz.a -- example/minigzip don't cross-link)
# --------------------------------------------------------------------------
ZLIB_SRC="$DEPS/zlib-src/zlib-1.3.1"
ZLIB_PREFIX="$DEPS/zlib-retro68"
ZLIB_TAR="$DEPS/zlib-1.3.1.tar.gz"
ZLIB_SHA=17e88863f3600672ab49182f217281b6fc4d3c762bde361935e436a95214d05c

if [ ! -f "$ZLIB_PREFIX/lib/libz.a" ]; then
    echo ">> zlib 1.3.1 (retro68) ..."
    fetch_verify "$ZLIB_TAR" "$ZLIB_SHA" \
        https://github.com/madler/zlib/archive/refs/tags/v1.3.1.tar.gz
    rm -rf "$DEPS/zlib-src" "$ZLIB_PREFIX"; mkdir -p "$DEPS/zlib-src"
    tar -xzf "$ZLIB_TAR" -C "$DEPS/zlib-src"
    ( cd "$ZLIB_SRC"
      CC=powerpc-apple-macos-gcc AR=powerpc-apple-macos-ar \
          RANLIB=powerpc-apple-macos-ranlib \
          ./configure --static --prefix="$ZLIB_PREFIX" >/dev/null
      # zlib's macOS configure sets AR=libtool -o; host libtool drops the
      # non-Mach-O Retro68 objects and leaves a 96-byte EMPTY libz.a.  Archive
      # with the Retro68 ar instead, and skip example/minigzip (they don't
      # cross-link).  Build just the library.
      make -j"$JOBS" libz.a AR=powerpc-apple-macos-ar ARFLAGS=rc >/dev/null
      mkdir -p "$ZLIB_PREFIX/include" "$ZLIB_PREFIX/lib"
      cp zlib.h zconf.h "$ZLIB_PREFIX/include/"
      cp libz.a "$ZLIB_PREFIX/lib/" )
else
    echo ">> zlib (retro68) already built"
fi

echo
echo "M-tier the_Foundation C libs ready."
echo "  PCRE2 : $PCRE2_PREFIX (pcre2.h, libpcre2-8.a, libpcre2-posix.a)"
echo "  zlib  : $ZLIB_PREFIX (zlib.h, libz.a)"
echo "  Consume: the_Foundation UNISTRING_DIR (libunistring) + PCRE2/zlib dirs as in the darwin8 build."
