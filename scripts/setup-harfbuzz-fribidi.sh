#!/usr/bin/env bash
#
# Cross-build the TEXT-SHAPING libraries the lagrange widget kit needs on the
# classic-Mac M-tier (Retro68 / OS 8/9 PPC): HarfBuzz + FriBidi.  These link
# into the renderer/fontpack (src/render/*, src/fontpack.c), NOT the_Foundation.
#
#   deps/harfbuzz-retro68   HarfBuzz 2.8.2  (src/hb.h, libharfbuzz.a)
#   deps/fribidi-retro68    FriBidi 1.0.13  (include/fribidi/*.h, libfribidi.a)
#
# Build notes (see docs/arcana.md "the_Foundation classic C libs"):
#   * Both are built WITHOUT glib/freetype/icu/cairo: lagrange uses HarfBuzz's
#     DEFAULT font funcs (hb_face_create + hb_font_create, fontpack.c) and
#     rasterizes glyphs itself via stb_truetype, so no FreeType is needed.
#     All optional deps are disabled in meson.
#   * HarfBuzz 2.8.2 is meson-only (no autotools).  Cross-built with a meson
#     cross-file; HB_NO_MT is required because Retro68 has pthread_mutex_t but
#     not the pthread_mutex_* functions (classic Mac = Thread Manager).
#   * FriBidi's CLI tool (bin/fribidi-main.c) fails on Retro68; build+install
#     only the library (make -C lib).
#
# Usage: scripts/setup-harfbuzz-fribidi.sh    (native host; needs Retro68 toolchain
#                                              + meson + ninja on PATH)
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
DEPS="$ROOT/vendor/ClassicNet/deps"
TC="${RETRO68_TOOLCHAIN:-$HOME/classic/Retro68-build/toolchain}"
export PATH="$TC/bin:$PATH"
JOBS="$(getconf _NPROCESSORS_ONLN 2>/dev/null || echo 4)"
for t in powerpc-apple-macos-gcc powerpc-apple-macos-g++ powerpc-apple-macos-ar powerpc-apple-macos-ranlib; do
    command -v "$t" >/dev/null || { echo "!! $t not found under $TC (set RETRO68_TOOLCHAIN)" >&2; exit 1; }
done
command -v meson >/dev/null || { echo "!! meson not on PATH (brew install meson ninja)" >&2; exit 1; }
mkdir -p "$DEPS"

fetch_verify() { # <tarball> <sha256> <url>
    if [ ! -f "$1" ]; then
        curl -fsSL --max-time 120 -o "$1" "$3"
    fi
    echo "$2  $1" | shasum -a 256 -c - >/dev/null
}

# --------------------------------------------------------------------------
# FriBidi 1.0.13 (autotools; build + install only the library)
# --------------------------------------------------------------------------
FR_SRC="$DEPS/fribidi-src/fribidi-1.0.13"
FR_BUILD="$DEPS/fribidi-retro68-build"
FR_PREFIX="$DEPS/fribidi-retro68"
FR_TAR="$DEPS/fribidi-1.0.13.tar.xz"
FR_SHA=7fa16c80c81bd622f7b198d31356da139cc318a63fc7761217af4130903f54a2

if [ ! -f "$FR_PREFIX/lib/libfribidi.a" ]; then
    echo ">> FriBidi 1.0.13 (retro68) ..."
    fetch_verify "$FR_TAR" "$FR_SHA" \
        https://github.com/fribidi/fribidi/releases/download/v1.0.13/fribidi-1.0.13.tar.xz
    rm -rf "$DEPS/fribidi-src" "$FR_BUILD"; mkdir -p "$DEPS/fribidi-src" "$FR_BUILD"
    tar xJf "$FR_TAR" -C "$DEPS/fribidi-src"
    ( cd "$FR_BUILD"
      "$FR_SRC/configure" --host=powerpc-apple-macos --prefix="$FR_PREFIX" \
          --disable-shared --enable-static >/dev/null
      # The fribidi CLI (bin/fribidi-main.c) #defines `false` and fails on
      # Retro68; we only need the library.
      make -C lib -j"$JOBS" >/dev/null
      make -C lib install >/dev/null )
else
    echo ">> FriBidi (retro68) already built"
fi

# --------------------------------------------------------------------------
# HarfBuzz 2.8.2 (meson-only; cross-file; HB_NO_MT; all optional deps OFF)
# --------------------------------------------------------------------------
HB_NAME=harfbuzz-2.8.2
HB_SRC="$DEPS/hb-src/$HB_NAME"
HB_BUILD="$DEPS/harfbuzz-retro68-build"
HB_PREFIX="$DEPS/harfbuzz-retro68"
HB_TAR="$DEPS/harfbuzz-2.8.2.tar.gz"
HB_SHA=4164f68103e7b52757a732227cfa2a16cfa9984da513843bb4eb7669adc6f220
HB_CROSS="$DEPS/retro68.meson-cross.txt"

if [ ! -f "$HB_PREFIX/lib/libharfbuzz.a" ]; then
    echo ">> HarfBuzz 2.8.2 (retro68) ..."
    fetch_verify "$HB_TAR" "$HB_SHA" \
        https://github.com/harfbuzz/harfbuzz/archive/refs/tags/2.8.2.tar.gz
    rm -rf "$DEPS/hb-src" "$HB_BUILD"; mkdir -p "$DEPS/hb-src"
    tar xzf "$HB_TAR" -C "$DEPS/hb-src"
    # Retro68 is 'darwin'/mach-o-ish but classic Mac; cpu ppc, big endian.
    cat > "$HB_CROSS" <<EOF
[binaries]
c = '$TC/bin/powerpc-apple-macos-gcc'
cpp = '$TC/bin/powerpc-apple-macos-g++'
ar = '$TC/bin/powerpc-apple-macos-ar'
strip = '$TC/bin/powerpc-apple-macos-strip'
ranlib = '$TC/bin/powerpc-apple-macos-ranlib'

[host_machine]
system = 'darwin'
cpu_family = 'ppc'
cpu = 'ppc'
endian = 'big'
EOF
    # HB_NO_MT: Retro68 has pthread_mutex_t but no pthread_mutex_* functions ->
    # hb-mutex.hh would fail.  No-op mutex is fine (single-threaded target; the
    # glyph output is untouched).  -Wno-format: uint32_t/codepoint_t is
    # `unsigned long` on Retro68, so %u is TYPE-, not ABI-, wrong (both 32-bit).
    meson setup "$HB_BUILD" "$HB_SRC" --cross-file "$HB_CROSS" \
        --buildtype=release -Ddefault_library=static \
        -Dglib=disabled -Dgobject=disabled -Dcairo=disabled -Dchafa=disabled \
        -Dicu=disabled -Dgraphite=disabled -Dfreetype=disabled -Dgdi=disabled \
        -Ddirectwrite=disabled -Dcoretext=disabled -Dtests=disabled \
        -Dintrospection=disabled -Ddocs=disabled -Dbenchmark=disabled \
        -Dexperimental_api=false \
        -Dc_args="-DHB_NO_MT -Wno-format" -Dcpp_args="-DHB_NO_MT -Wno-format" \
        >/dev/null
    ninja -C "$HB_BUILD" >/dev/null
    meson configure "$HB_BUILD" -Dprefix="$HB_PREFIX" >/dev/null
    meson install -C "$HB_BUILD" >/dev/null
else
    echo ">> HarfBuzz (retro68) already built"
fi

echo
echo "Text-shaping libs ready for the M-tier renderer."
echo "  HarfBuzz : $HB_PREFIX (include/harfbuzz/hb.h, lib/libharfbuzz.a)"
echo "  FriBidi  : $FR_PREFIX (include/fribidi/fribidi.h, lib/libfribidi.a)"
echo "  Wire: add both to the Classic renderer target, define LAGRANGE_ENABLE_HARFBUZZ=1 + LAGRANGE_ENABLE_FRIBIDI=1."
