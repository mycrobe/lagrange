#!/usr/bin/env bash
#
# Build the Classic (Mac OS 9 PPC) flavor's *network-first* slice -- the
# M-tier analog of scripts/build-osx.sh.  Cross-builds the vendored ClassicNet
# OT slice (cn_ot Open Transport + cn_tls mbedTLS + cn_mac_time) and cy384's
# PPC mbedTLS into a Retro68 .bin, proving the network seam is portable to
# classic Mac OS before any UI.
#
#   scripts/build-mac.sh           -> build-mac/cn_ot_smoke.bin (+ .APPL)
#   scripts/build-mac.sh --seam    -> build-mac-seam/ (the_Foundation +
#                                     ClassicNet OT seam + tls_smoke.bin)
#   scripts/build-mac.sh --debug   -> build-mac-debug/ (trace build)
#
# Prereqs:
#   * Retro68 toolchain (RETRO68_TOOLCHAIN, default ~/classic/Retro68-build/toolchain)
#   * PPC mbedTLS built: vendor/ClassicNet/scripts/setup-mbedtls.sh (the PPC
#     step; the vendored script's bash line-continuation bug means the build
#     may need to be re-run manually -- see mac/CMakeLists.txt)
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
TC="${RETRO68_TOOLCHAIN:-$HOME/classic/Retro68-build/toolchain}"
TCFILE="$TC/powerpc-apple-macos/cmake/retroppc.toolchain.cmake"
[ -f "$TCFILE" ] || { echo "Retro68 toolchain not found at $TCFILE (set RETRO68_TOOLCHAIN)"; exit 1; }

# cy384's classic-Mac mbedTLS fork (PPC build).  Starscape's recipe; see
# vendor/ClassicNet/scripts/setup-mbedtls.sh.
MBEDTLS_PPC="$ROOT/vendor/ClassicNet/deps/mbedtls-ppc"
if [ ! -f "$MBEDTLS_PPC/build-ppc/library/libmbedtls.a" ]; then
    echo "!! mbedTLS (ppc) missing at $MBEDTLS_PPC" >&2
    echo "   run: vendor/ClassicNet/scripts/setup-mbedtls.sh  (the PPC step)" >&2
    exit 1
fi

BDIR="$ROOT/build-mac"
EXTRA=("${EXTRA[@]:-}")
while [ $# -gt 0 ]; do
    case "$1" in
        --debug) EXTRA=(-DCMAKE_BUILD_TYPE=Debug); BDIR="$ROOT/build-mac-debug" ;;
        --seam)  EXTRA+=(-DLAGRANGE_TFDN_SEAM=ON); BDIR="$ROOT/build-mac-seam" ;;
        *) echo "unknown arg: $1" >&2; exit 2 ;;
    esac
    shift
done

cmake -S "$ROOT/mac" -B "$BDIR" \
    -DCMAKE_TOOLCHAIN_FILE="$TCFILE" \
    -DMBEDTLS_PPC_ROOT="$MBEDTLS_PPC" \
    "${EXTRA[@]}"
cmake --build "$BDIR" -j"$(getconf _NPROCESSORS_ONLN 2>/dev/null || echo 4)"

echo
if [ "$BDIR" = "$ROOT/build-mac-seam" ]; then
    echo "built: $BDIR/tls_smoke.bin (+ cn_ot_smoke.bin)"
    echo "  push to the guest: ~/classic/vm/bin/deploy-qemu.sh --vm macos9 $BDIR/tls_smoke.bin"
else
    echo "built: $BDIR/cn_ot_smoke.bin"
    echo "  push to the guest: ~/classic/vm/bin/deploy-qemu.sh --vm macos9 $BDIR/cn_ot_smoke.bin"
fi
