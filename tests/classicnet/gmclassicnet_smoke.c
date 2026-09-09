/*
 * N1 host smoke: a real Gemini fetch over ClassicNet's cn_darwin8 (BSD
 * sockets) + cn_tls (mbedTLS) transports, driven directly through the
 * CNTransport vtable -- the same seam the Socket/TlsRequest backends (N2) will
 * sit behind. Unlike ClassicNet's HTTP-only CNRequest, this talks the Gemini
 * wire myself: send the absolute `gemini://host/path` URL line, read the
 * `<STATUS> <META>\r\n` head, and assert a 2x status + a non-empty body.
 *
 * Usage:
 *   gmclassicnet_smoke <host> <port> <path> [ca.pem]
 *
 * host   : connect/SNI hostname. The server cert must chain to <ca.pem> and
 *          carry this host as CN/SAN (verification is REQUIRED when <ca.pem>
 *          is given; without it the run is insecure and escapes the TOFU gate).
 * port   : TCP port (default for a Gemini capsule is 1965).
 * path   : the URL path, e.g. "/". Joins with the host as gemini://host+path.
 * ca.pem : optional PEM CA bundle used to verify the server certificate.
 *
 * Exit 0 on success (2x status + non-empty body); non-zero on failure.
 */
#include "classicnet/cn_darwin8.h"
#include "classicnet/cn_tls.h"
#include "classicnet/cn_transport.h"
#include "classicnet/cn_errors.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define HEAD_BUF 8192

static int die(const char *what, OSStatus s)
{
    printf("FAIL: %s -> %d\n", what, (int)s);
    return 1;
}

int main(int argc, char **argv)
{
    const char *host    = argc > 1 ? argv[1] : "localhost";
    UInt16      port    = (UInt16)(argc > 2 ? atoi(argv[2]) : 1965);
    const char *path    = argc > 3 ? argv[3] : "/";
    const char *caPath  = argc > 4 ? argv[4] : NULL;

    static char caBuf[400000];
    const char *caPem = NULL;
    UInt32      caLen = 0;

    CNDarwin8Transport tcp;
    CNTlsTransport     tls;
    CNTransport       *tcpT = NULL, *tlsT = NULL;
    char req[512];
    UInt32 reqLen, off = 0;
    static char resp[HEAD_BUF];
    UInt32 rlen = 0;
    int guard, i, headLine;
    OSStatus s;

    if (caPath) {
        FILE *f = fopen(caPath, "rb");
        if (!f) { printf("FAIL: cannot open CA %s\n", caPath); return 1; }
        caLen = (UInt32)fread(caBuf, 1, sizeof(caBuf) - 1, f);
        fclose(f);
        caBuf[caLen] = '\0';
        caLen += 1;                      /* mbedTLS PEM parse wants the NUL in */
        caPem = caBuf;
        printf("verify: REQUIRED (CA %s, host '%s')\n", caPath, host);
    } else {
        printf("verify: NONE (insecure)\n");
    }

    if (CN_Darwin8Create(&tcp, host, port, &tcpT) != noErr)
        return die("tcp connect", kCNErrNetIo);
    if (CN_TlsCreate(&tls, tcpT, host, caPem, caLen, &tlsT) != noErr)
        return die("tls create", kCNErrTlsInit);

    /* --- connect phase (non-blocking; wait on socket write readiness) --- */
    s = tcpT->poll(tcpT);
    while (s == kCNErrWouldBlock) {
        if (CN_Darwin8Wait(&tcp, 250, 1) != noErr)
            return die("tcp wait", kCNErrNetIo);
        s = tcpT->poll(tcpT);
    }
    if (s != noErr) return die("tcp connect", s);

    /* --- TLS handshake (mbedTLS; wait on socket read readiness) --- */
    for (guard = 0; !tls.handshakeDone && guard < 4000; guard++) {
        s = tlsT->poll(tlsT);
        if (s == noErr) break;
        if (s != kCNErrWouldBlock) {
            printf("FAIL: tls handshake (mbedTLS rc=%d -0x%04X)\n",
                   tls.lastError, (unsigned)(-tls.lastError));
            CN_TlsDispose(&tls);
            tcpT->close(tcpT);
            return 1;
        }
        if (CN_Darwin8Wait(&tcp, 20, 0) != noErr) { /* timeout is fine; re-poll */
        }
    }
    if (!tls.handshakeDone) {
        printf("FAIL: tls handshake timeout (mbedTLS rc=%d -0x%04X)\n",
               tls.lastError, (unsigned)(-tls.lastError));
        CN_TlsDispose(&tls);
        tcpT->close(tcpT);
        return 1;
    }

    /* --- send the Gemini URL line --- */
    reqLen = (UInt32)snprintf(req, sizeof(req), "gemini://%s%s\r\n", host, path);
    if (reqLen >= sizeof(req)) 
        return die("request too long", kCNErrBufferOverflow);
    while (off < reqLen) {
        UInt32 got = 0;
        s = tlsT->send(tlsT, req + off, reqLen - off, &got);
        if (s != noErr) return die("request send", s);
        if (got == 0) {
            if (CN_Darwin8Wait(&tcp, 50, 1) != noErr) { /* re-poll */ }
            continue;
        }
        off += got;
    }

    /* --- read the response; a Gemini server responds then closes --- */
    for (guard = 0; rlen < sizeof(resp) && guard < 2000; guard++) {
        Boolean eof = false;
        UInt32 got = 0;
        s = tlsT->recv(tlsT, resp + rlen, sizeof(resp) - rlen, &got, &eof);
        if (s != noErr) return die("response recv", s);
        if (got > 0) rlen += got;
        if (eof) break;
        if (got == 0) CN_Darwin8Wait(&tcp, 40, 0);
    }

    /* --- parse the head --- */
    headLine = -1;
    for (i = 0; i + 1 < (int)rlen; i++)
        if (resp[i] == '\r' && resp[i + 1] == '\n') { headLine = i; break; }

    CN_TlsDispose(&tls);
    tcpT->close(tcpT);

    if (rlen < 2 || headLine < 0) {
        printf("FAIL: no response head (rlen=%u)\n", rlen);
        return 1;
    }
    if (resp[0] != '2' || resp[1] < '0' || resp[1] > '9') {
        printf("FAIL: status '%.2s' not 2x (head: %.*s)\n",
               resp, (headLine < 60 ? headLine : 60), resp);
        return 1;
    }
    {
        UInt32 bodyLen = rlen - (UInt32)(headLine + 2);
        if (bodyLen == 0) {
            printf("FAIL: status '%.2s' with empty body (head: %.*s)\n",
                   resp, (headLine < 60 ? headLine : 60), resp);
            return 1;
        }
        printf("OK: Gemini status '%.2s' head '%.*s' -> %u body bytes\n",
               resp, (headLine < 60 ? headLine : 60), resp, bodyLen);
    }
    return 0;
}
