#!/usr/bin/env bash
#
# N1 host gate -- ClassicNet network seam (host wiring).
#
# Proves, on the host:
#   1) ClassicNet's own host slice stays 13/13 under ASan (incl. test_darwin8,
#      the transport, and test_h2_download, real I/O), built from the vendored
#      submodule + the provisioned host mbedTLS 3.6.
#   2) A real Gemini fetch over cn_darwin8 + cn_tls returns a 2x status with a
#      non-empty body (the smoke-fetch gate), verified against a test CA.
#
# Usage:
#   scripts/test-classicnet-n1.sh [gmclassicnet_smoke-binary]
#
# env:
#   N1_PORT   TCP port for the local test server (default 19650)
#
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
CN="$ROOT/vendor/ClassicNet"
CN_BUILD="$CN/build-host-tls3"
PORT="${N1_PORT:-19650}"
JOBS="$(getconf _NPROCESSORS_ONLN 2>/dev/null || echo 4)"

SMOKE="${1:-$ROOT/build-classicnet/gmclassicnet_smoke}"
if [ ! -x "$SMOKE" ]; then
    echo "gmclassicnet_smoke not built. Configure with:"
    echo "  cmake -S . -B build-classicnet -DENABLE_CLASSICNET=ON -DENABLE_GUI=OFF"
    echo "  cmake --build build-classicnet --target gmclassicnet_smoke"
    exit 1
fi

# --- 0) provision the vendored submodule + host mbedTLS if missing -------
if [ ! -f "$CN/deps/mbedtls-host3/library/libmbedtls.a" ]; then
    "$ROOT/scripts/setup-classicnet.sh"
fi

TMP="$(mktemp -d)"
SRV=0
# On any exit, stop the server and surface the smoke's output for the record.
trap 'if [ "$SRV" -ne 0 ]; then kill "$SRV" 2>/dev/null || true; fi; [ -f "$TMP/client.out" ] && cat "$TMP/client.out" || true; rm -rf "$TMP" || true' EXIT

# --- 1) ClassicNet host slice: 13/13 (ASan) ------------------------------
if [ ! -d "$CN_BUILD" ]; then
    cmake -S "$CN" -B "$CN_BUILD" \
        -DCN_WITH_MBEDTLS=ON -DMBEDTLS_ROOT="$CN/deps/mbedtls-host3"
fi
echo ">> building ClassicNet host slice ..."
cmake --build "$CN_BUILD" -j"$JOBS" >/dev/null
echo ">> ClassicNet host slice tests ..."
if ! ctest --test-dir "$CN_BUILD" >"$TMP/ctest.out" 2>&1; then
    echo "!! ClassicNet host slice tests FAILED"; grep -E "Failed|Test #" "$TMP/ctest.out"; exit 1
fi
if ! grep -q "100% tests passed" "$TMP/ctest.out"; then
    echo "!! ClassicNet host slice not all tests passed"; tail -20 "$TMP/ctest.out"; exit 1
fi
echo "OK: ClassicNet host slice 13/13 (ASan)"

# --- 2) throwaway PKI: CA + server cert (CN/SAN = localhost) -------------
openssl req -x509 -newkey rsa:2048 -nodes -keyout "$TMP/ca-key.pem" \
    -out "$TMP/ca.pem" -days 1 -subj "/CN=N1 Test CA" >/dev/null 2>&1
openssl req -newkey rsa:2048 -nodes -keyout "$TMP/srv-key.pem" \
    -out "$TMP/srv.csr" -subj "/CN=localhost" >/dev/null 2>&1
openssl x509 -req -in "$TMP/srv.csr" -CA "$TMP/ca.pem" -CAkey "$TMP/ca-key.pem" \
    -CAcreateserial -days 1 -extfile <(printf 'subjectAltName=DNS:localhost') \
    -out "$TMP/srv.pem" >/dev/null 2>&1

# --- 3) local TLS Gemini server ------------------------------------------
python3 "$ROOT/tests/classicnet/gemini_tls_server.py" \
    "$TMP/srv.pem" "$TMP/srv-key.pem" "$PORT" >"$TMP/server.out" 2>&1 &
SRV=$!
for _ in $(seq 1 50); do
    if grep -q "ready" "$TMP/server.out" 2>/dev/null; then break; fi
    sleep 0.1
done

# --- 4) the smoke fetch (verified against the test CA) -------------------
echo ">> running Gemini smoke fetch (verified) ..."
"$SMOKE" localhost "$PORT" "/" "$TMP/ca.pem"
echo "OK: Gemini smoke fetch -> 2x status + non-empty body"
