#!/usr/bin/env bash
#
# Cross-build THE GNU libunistring the_Foundation depends on, for a classic-Mac
# target.  libunistring is a MANDATORY the_Foundation dependency (Depends.cmake
# FATAL_ERRORs without unistr.h; string.c / block.c / punycode.c call
# u8_normalize / u8_mbsnlen / u8_check / u8_to_u32 / u8_conv_* / u8_toupper /
# u8_casecmp ...).  It is absent from PLAN.md Phase 2's dep list -- this
# provides it for both tiers.  See docs/arcana.md "libunistring for classic-Mac".
#
# Usage:
#   scripts/setup-libunistring.sh [retro68|darwin8]   (default: retro68)
#
# Output (gitignored, under vendor/ClassicNet/deps/):
#   deps/libunistring-1.4.2.tar.xz{.sig}   GNU release tarball + detached sig
#   deps/libunistring-src/                 extracted source (patched in place)
#   deps/libunistring-<target>/            --prefix install tree
#       include/unistr.h + ...             public headers
#       lib/libunistring.a                 cross-compiled static archive
#
# Targets:
#   retro68  M-tier, Mac OS 8/9 PPC.  NATIVE on this Mac (Retro68 is a host
#            Darwin cross-compiler).  Needs ~/classic/Retro68-build/toolchain.
#   darwin8  T-tier, OS X 10.4 PPC.  The powerpc-apple-darwin8-gcc cross
#            compiler only runs inside the amd64 docker container, so this
#            branch must be run in the scripts/build-osx.sh context (which sets
#            up the /sdk mount + PATH).
set -euo pipefail

target="${1:-retro68}"
version=1.4.2
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
DEPS="$ROOT/vendor/ClassicNet/deps"
TARBALL="$DEPS/libunistring-$version.tar.xz"
SIG="$TARBALL.sig"
SRC="$DEPS/libunistring-src"
PREFIX="$DEPS/libunistring-$target"
BUILD="$DEPS/libunistring-$target-build"
JOBS="$(getconf _NPROCESSORS_ONLN 2>/dev/null || echo 4)"

case "$target" in
    retro68)
        host=powerpc-apple-macos
        gcc=powerpc-apple-macos-gcc; ar=powerpc-apple-macos-ar
        ranlib=powerpc-apple-macos-ranlib; strip=powerpc-apple-macos-strip
        nmtool=powerpc-apple-macos-nm
        export PATH="$HOME/classic/Retro68-build/toolchain/bin:$PATH"
        # Classic Mac has Open Transport, NOT BSD sockets, so gnulib's socklen_t
        # probe hard-errors ("Cannot find a type to use in place of socklen_t").
        # Short-circuit via the cached answer -> config.h '#define socklen_t int'.
        # Classic Mac libc also has NO iconv, so build the uniconv u8_conv_*
        # objects WITHOUT iconv (self-contained archive, no iconv_open refs);
        # the T-tier keeps libSystem iconv instead.
        configure_cache=(gl_cv_socklen_t_equiv=int ac_cv_header_iconv_h=no am_cv_func_iconv=no)
        ;;
    darwin8)
        host=powerpc-apple-darwin8
        gcc=powerpc-apple-darwin8-gcc; ar=powerpc-apple-darwin8-ar
        ranlib=powerpc-apple-darwin8-ranlib; strip=powerpc-apple-darwin8-strip
        nmtool=powerpc-apple-darwin8-nm
        # Darwin8 (Tiger) HAS sys/socket.h (socklen_t found natively) and libSystem
        # iconv (the osx build sets UNISTRING_ICONV=NO for that reason), so no
        # configure-cache overrides.  The darwin8 cross compiler must already be
        # on PATH (it lives in the amd64 container; see scripts/build-osx.sh).
        configure_cache=()
        ;;
    *)
        echo "unknown target: $target (expected retro68|darwin8)" >&2
        exit 1
        ;;
esac

mkdir -p "$DEPS"

echo ">> GNU libunistring $version -- download + signature check ..."
if [ ! -f "$TARBALL" ]; then
    curl -fL -o "$TARBALL" "https://ftp.gnu.org/gnu/libunistring/libunistring-$version.tar.xz"
    curl -fL -o "$SIG"     "https://ftp.gnu.org/gnu/libunistring/libunistring-$version.tar.xz.sig"
fi
if [ ! -f "$DEPS/gnu-keyring.gpg" ]; then
    curl -fL -o "$DEPS/gnu-keyring.gpg" "https://ftp.gnu.org/gnu/gnu-keyring.gpg"
fi
# Verify the tarball before we build anything from it.  Preferred: GNU's
# detached sig via gpgv (gpg isn't on this box; gpgv may only exist inside the
# build container).  Fallback: a pinned SHA-256 of the same verified download
# (the 1.4.2 release), so the host Retro68 build stays reproducible without gpg.
if command -v gpgv >/dev/null 2>&1; then
    gpgv --keyring "$DEPS/gnu-keyring.gpg" "$SIG" "$TARBALL"
else
    echo "  (gpgv not on PATH -- verifying against pinned SHA-256 instead)"
    echo "5b46e74377ed7409c5b75e7a96f95377b095623b689d8522620927964a41499c  $TARBALL" | shasum -a 256 -c -
fi

if [ -e "$SRC" ]; then rm -rf "$SRC"; fi
tar -xf "$TARBALL" -C "$DEPS"
mv "$DEPS/libunistring-$version" "$SRC"

# gnulib's getlocalename_l-unsafe.c ends in a hard #error on unknown platforms.
# darwin8 defines __APPLE__ && __MACH__ so it takes gnulib's Mac OS X branch and
# never reaches it; Retro68's compiler defines __PPC__/__powerpc__ only, so it
# needs a no-locale fallback (the_Foundation works on byte strings, which are
# locale-independent; classic Mac has no per-locale names anyway).
if [ "$target" = retro68 ]; then
    perl -0pi -e 's/\n#else\n[ \t]*#error "Please port gnulib getlocalename_l-unsafe\.c to your platform! Report this to bug-gnulib\."\n#endif/\n#elif defined __PPC__ || defined __powerpc__ || defined __MACH__\n      \/* Classic Mac OS: no per-locale names. *\/\n      return (struct string_with_storage) { "C", STORAGE_INDEFINITE };\n#else\n #error "Please port gnulib getlocalename_l-unsafe.c to your platform! Report this to bug-gnulib."\n#endif/' \
        "$SRC/lib/getlocalename_l-unsafe.c"
fi

echo ">> libunistring $target build ($host) ..."
rm -rf "$BUILD"; mkdir -p "$BUILD"
(
    cd "$BUILD"
    "$SRC/configure" --host="$host" --prefix="$PREFIX" \
        --disable-shared --enable-static \
        CC="$gcc" AR="$ar" RANLIB="$ranlib" STRIP="$strip" NM="$nmtool" \
        "${configure_cache[@]}"
    # gnulib's thread probe is fooled on the classic SDKs: pthread_create is an
    # inline, so gnulib concludes "no real pthread API" (PTHREAD_CREATE_IS_INLINE=1)
    # and mbtowc-lock.h matches NO branch -> mbtowc_with_lock undefined and
    # mbrtowc.c/mbrtoc32.c fail.  AVOID_ANY_THREADS routes it to the self-contained
    # no-lock branch (fine: the_Foundation uses UTF-8 byte strings, and these
    # targets are single-threaded by design).
    sed -i '' 's|/\* #undef AVOID_ANY_THREADS \*/|#define AVOID_ANY_THREADS 1|' config.h
    make -C lib -j"$JOBS"
    make -C lib install
)

if [ ! -f "$PREFIX/include/unistr.h" ]; then
    echo "!! failed to produce unistr.h at $PREFIX/include/unistr.h" >&2
    exit 1
fi
echo
echo "libunistring ($target) built -> $PREFIX"
echo "  headers : $PREFIX/include/unistr.h"
echo "  static  : $PREFIX/lib/libunistring.a"
echo "  consume : point the_Foundation's UNISTRING_DIR at it"
