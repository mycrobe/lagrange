#!/usr/bin/env bash
#
# N2 step-2 host gate -- ClassicNet-backed iTlsRequest + iTlsCertificate over
# cn_tls (mbedTLS).
#
# Proves, on the host:
#   1) A real Gemini fetch through the the_Foundation TlsRequest backend
#      (CA-verified path): the server cert chains to a test CA and
#      verify_TlsCertificate() reports authority.
#   2) The TOFU path: with no CA bundle, the app's verify func accepts the
#      leaf during the handshake and the fetch still succeeds, but the cert is
#      NOT CA-authority -- exactly the lagrange self-signed/TofU gate shape.
#
# Both phases run against a local TLS Gemini server under ASan/UBSan.
#
# Usage:
#   scripts/test-classicnet-tls.sh [t_classicnet_tls-binary]
#
# env:
#   N2_TLS_PORT   TCP port for the local test server (default 19660)
#
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
PORT="${N2_TLS_PORT:-19660}"
JOBS="$(getconf _NPROCESSORS_ONLN 2>/dev/null || echo 4)"

BIN="${1:-$ROOT/build-classicnet/t_classicnet_tls}"
if [ ! -x "$BIN" ]; then
    echo "t_classicnet_tls not built. Configure with:"
    echo "  cmake -S . -B build-classicnet -DENABLE_CLASSICNET=ON -DENABLE_GUI=OFF"
    echo "  cmake --build build-classicnet --target t_classicnet_tls"
    exit 1
fi

TMP="$(mktemp -d)"
SRV=0
trap 'if [ "$SRV" -ne 0 ]; then kill "$SRV" 2>/dev/null || true; fi; rm -rf "$TMP" || true' EXIT

# --- throwaway PKI: CA + server cert (CN/SAN = localhost) -----------------
openssl req -x509 -newkey rsa:2048 -nodes -keyout "$TMP/ca-key.pem" \
    -out "$TMP/ca.pem" -days 1 -subj "/CN=N2 Test CA" >/dev/null 2>&1
openssl req -newkey rsa:2048 -nodes -keyout "$TMP/srv-key.pem" \
    -out "$TMP/srv.csr" -subj "/CN=localhost" >/dev/null 2>&1
openssl x509 -req -in "$TMP/srv.csr" -CA "$TMP/ca.pem" -CAkey "$TMP/ca-key.pem" \
    -CAcreateserial -days 1 -extfile <(printf 'subjectAltName=DNS:localhost') \
    -out "$TMP/srv.pem" >/dev/null 2>&1

# --- local TLS Gemini server ----------------------------------------------
python3 "$ROOT/tests/classicnet/gemini_tls_server.py" \
    "$TMP/srv.pem" "$TMP/srv-key.pem" "$PORT" >"$TMP/server.out" 2>&1 &
SRV=$!
for _ in $(seq 1 50); do
    if grep -q "ready" "$TMP/server.out" 2>/dev/null; then break; fi
    sleep 0.1
done

echo ">> running ClassicNet TlsRequest test (CA + TOFU phases) ..."
"$BIN" localhost "$PORT" "/" "$TMP/ca.pem"
echo "OK: ClassicNet TlsRequest backend -> CA-verified fetch + TOFU-accepted fetch"
