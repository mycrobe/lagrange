#!/usr/bin/env bash
#
# N2 host unit test -- ClassicNet-backed iSocket over a loopback TCP transport.
# Starts a local echo server and runs t_classicnet_socket against it under
# ASan/UBSan (the binary is built in build-classicnet, which compiles the_Foundation
# with the classicnet socket backend via TFDN_CLASSICNET).
#
# Usage:
#   scripts/test-classicnet-socket.sh [t_classicnet_socket-binary]
#
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
PORT="${N1_PORT:-19651}"

BIN="${1:-$ROOT/build-classicnet/t_classicnet_socket}"
if [ ! -x "$BIN" ]; then
    echo "t_classicnet_socket not built. Configure with:"
    echo "  cmake -S . -B build-classicnet -DENABLE_CLASSICNET=ON -DENABLE_GUI=OFF"
    echo "  cmake --build build-classicnet --target t_classicnet_socket"
    exit 1
fi

TMP="$(mktemp -d)"
SRV=0
trap 'if [ "$SRV" -ne 0 ]; then kill "$SRV" 2>/dev/null || true; fi; rm -rf "$TMP" || true' EXIT

python3 "$ROOT/tests/classicnet/socket_echo_server.py" "$PORT" >"$TMP/server.out" 2>&1 &
SRV=$!
for _ in $(seq 1 50); do
    if grep -q "ready" "$TMP/server.out" 2>/dev/null; then break; fi
    sleep 0.1
done

echo ">> running ClassicNet iSocket loopback test ..."
"$BIN" 127.0.0.1 "$PORT"
