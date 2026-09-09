#!/usr/bin/env bash
#
# Provision the ClassicNet host slice (Phase 1 -- N1 wiring).
#
#   vendor/ClassicNet        git submodule (pinned commit, see .gitmodules)
#   vendor/ClassicNet/deps/mbedtls-host3
#                            host mbedTLS 3.6.0 build tree (vanilla, x86_64 host)
#                            -- the vanilla host mbedTLS ClassicNet's TLS layer
#                            links against on the host (same version as the PPC
#                            fork). Build artifact, gitignored; NOT shipped.
#
# The pin is the public origin/darwin8-transport tip (see arcana.md "ClassicNet
# submodule pin"): starscape's ClassicNet submodule is one local-ahead commit
# (57ca5db, client-cert identity) that was never pushed, so a reproducible
# remote fetch from this repo pins the origin tip 8e0df7a instead. Client-cert
# identity is a later milestone (Gemini auth); N1 does not need it.
#
# Usage: scripts/setup-classicnet.sh
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
CN="$ROOT/vendor/ClassicNet"
DEPS="$CN/deps"
JOBS="$(getconf _NPROCESSORS_ONLN 2>/dev/null || echo 4)"

echo ">> ClassicNet submodule ..."
git -C "$ROOT" submodule update --init vendor/ClassicNet
if [ ! -f "$CN/include/classicnet/cn_transport.h" ]; then
    echo "!! vendor/ClassicNet is missing its sources" >&2
    exit 1
fi

echo ">> host mbedTLS 3.6.0 (vanilla) ..."
if [ ! -f "$DEPS/mbedtls-host3/library/libmbedtls.a" ]; then
    mkdir -p "$DEPS"
    [ -d "$DEPS/mbedtls-host3" ] || \
        git clone --depth 1 --branch v3.6.0 \
            https://github.com/Mbed-TLS/mbedtls.git "$DEPS/mbedtls-host3"
    make -C "$DEPS/mbedtls-host3" -j"$JOBS" lib
else
    echo ">> host mbedTLS already built"
fi

echo
echo "ClassicNet host slice ready."
echo "  tests (13/13)       : cmake -S $CN -B $CN/build-host-tls3 -DCN_WITH_MBEDTLS=ON -DMBEDTLS_ROOT=$DEPS/mbedtls-host3"
echo "  lagrange build config: -DENABLE_CLASSICNET=ON"
