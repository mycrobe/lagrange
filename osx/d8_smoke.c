/*
 * d8_smoke -- first T-tier (Tiger 10.4 PPC) network proof for lagrange:
 * a CN-level Gemini fetch over cn_darwin8 (BSD sockets) + cn_tls (mbedTLS).
 *
 * This is the *cross-build* gate for the ClassicalNet darwin8 slice: it
 * links mbedTLS-d8 + cn_darwin8 + cn_tls into a PPC Mach-O, and (on petal)
 * proves the network seam is portable to Tiger before any UI exists.  It
 * does NOT depend on the_Foundation or lagrange's portable core -- the
 * the_Foundation seam is the next slice.
 *
 * verify-none (caPem==0): trust is the TOFU pin, layered by the app's
 * gmcerts above this seam; this tool just proves the transport works.
 *
 * usage: d8_smoke <host> [port] [path]
 *        e.g. d8_smoke localhost 1965 /
 *
 * POSIX layer (OS X flavor): stdio, gettimeofday (via CN_Darwin8Wait's poll).
 */
#include "classicnet/cn_darwin8.h"
#include "classicnet/cn_tls.h"
#include "classicnet/cn_errors.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SMOKE_BODY_CAP 262144   /* reply buffer: reserved once, never freed */
#define SMOKE_WAIT_MS  100      /* one wait slice */
#define SMOKE_MAX_TICKS 800     /* ~80 s at 100 ms */

int main(int argc, char **argv)
{
    const char *host = argc > 1 ? argv[1] : "127.0.0.1";
    UInt16      port = (UInt16)(argc > 2 ? atoi(argv[2]) : 1965);
    const char *path = argc > 3 ? argv[3] : "/";
    CNDarwin8Transport tcp;
    CNTlsTransport  tls;
    CNTransport    *tcpT, *tlsT;
    static char     body[SMOKE_BODY_CAP];
    static char     req[1024];
    char            urlHost[64];
    OSStatus        s;
    UInt32          reqLen, sent, got, total = 0;
    Boolean         eof;
    int             phase, ticks;

    /* Line-buffered stdout: evidence survives a late crash. */
    setvbuf(stdout, NULL, _IOLBF, 0);

    /* 1 -- TCP transport (DNS inside CN_Darwin8Create; blocking, capped). */
    if (CN_Darwin8Create(&tcp, host, port, &tcpT) != noErr) {
        printf("d8_smoke fail stage=1 err=create host=%s\n", host);
        return 1;
    }

    /* 2 -- TLS layer, VERIFY_NONE: trust is the TOFU pin, not a CA. */
    if (CN_TlsCreate(&tls, tcpT, host, NULL, 0, &tlsT) != noErr) {
        printf("d8_smoke fail stage=2 err=tlsinit host=%s\n", host);
        CN_Darwin8Dispose(&tcp);
        return 1;
    }

    /* 3a -- connect: write-waits until the TCP layer is up. */
    s = tcpT->poll(tcpT);
    ticks = 0;
    while (s == kCNErrWouldBlock && ticks++ < SMOKE_MAX_TICKS) {
        (void)CN_Darwin8Wait(&tcp, SMOKE_WAIT_MS, 1);
        s = tcpT->poll(tcpT);
    }
    if (s != noErr) {
        printf("d8_smoke fail stage=3a err=%ld host=%s\n", (long)s, host);
        CN_TlsDispose(&tls);
        CN_Darwin8Dispose(&tcp);
        return 1;
    }

    /* 3b -- handshake: read-waits until the TLS layer is up. */
    s = tlsT->poll(tlsT);
    ticks = 0;
    while (s == kCNErrWouldBlock && ticks++ < SMOKE_MAX_TICKS) {
        (void)CN_Darwin8Wait(&tcp, SMOKE_WAIT_MS, 0);
        s = tlsT->poll(tlsT);
    }
    if (s != noErr) {
        printf("d8_smoke fail stage=3b err=%ld tlsErr=%d host=%s\n",
               (long)s, tls.lastError, host);
        CN_TlsDispose(&tls);
        CN_Darwin8Dispose(&tcp);
        return 1;
    }

    /* 4 -- the exchange: URL + CRLF, then pump send/recv. */
    if (port == 1965)
        snprintf(urlHost, sizeof urlHost, "%s", host);
    else
        snprintf(urlHost, sizeof urlHost, "%s:%u", host, (unsigned)port);
    if (path[0] == '/')
        snprintf(req, sizeof req, "gemini://%s%s\r\n", urlHost, path);
    else
        snprintf(req, sizeof req, "gemini://%s/%s\r\n", urlHost, path);
    reqLen = (UInt32)strlen(req);

    phase = 0;   /* 0 = sending, 1 = receiving, 2 = done */
    for (ticks = 0; ticks < SMOKE_MAX_TICKS && phase < 2; ++ticks) {
        if (phase == 0) {
            sent = 0;
            s = tlsT->send(tlsT, req, reqLen, &sent);
            if (s != noErr) {
                printf("d8_smoke fail stage=4 err=send host=%s\n", host);
                phase = -1; break;
            }
            if (sent == reqLen) {
                phase = 1;
            } else {
                /* TLS would-block on send: wait for writability. */
                (void)CN_Darwin8Wait(&tcp, SMOKE_WAIT_MS, 1);
            }
        } else {
            eof = false;
            got = 0;
            s = tlsT->recv(tlsT, body + total, (UInt32)(sizeof(body) - total),
                           &got, &eof);
            if (s != noErr) {
                printf("d8_smoke fail stage=4 err=recv host=%s\n", host);
                phase = -1; break;
            }
            total += got;
            if (eof) { phase = 2; break; }
            if (got == 0) {
                /* TLS would-block on recv: wait for readability. */
                (void)CN_Darwin8Wait(&tcp, SMOKE_WAIT_MS, 0);
            }
            if (total >= sizeof(body)) { phase = 2; break; }
        }
    }

    CN_TlsDispose(&tls);
    CN_Darwin8Dispose(&tcp);

    if (phase != 2) {
        printf("d8_smoke fail stage=4 err=incomplete phase=%d host=%s\n",
               phase, host);
        return 1;
    }

    /* Report the Gemini status head + a count; the body head is a graceful
       hint (may be binary) so cap the print and mark a NUL. */
    body[total < sizeof(body) ? total : sizeof(body) - 1] = '\0';
    printf("d8_smoke ok host=%s port=%u path=%s bytes=%lu\n", host,
           (unsigned)port, path, (unsigned long)total);
    printf("----- body head -----\n%s\n----------------------\n",
           total > 0 ? body : "(empty)");
    return 0;
}
