/*
 * cn_ot_smoke -- first M-tier (Mac OS 9 PPC) network proof for lagrange:
 * a CN-level Gemini fetch over cn_ot (Open Transport) + cn_tls (mbedTLS-ppc).
 *
 * This is the *cross-build* gate for the ClassicNet OT slice: it links the
 * Retro68 PPC mbedTLS + cn_ot + cn_tls + cn_mac_time into a Mac OS 9 .bin,
 * and (on the QEMU macos9 guest) proves the network seam is portable to
 * Classic before any UI exists.  It does NOT depend on the_Foundation or
 * lagrange's portable core -- the the_Foundation seam is the next slice.
 *
 * verify-none (caPem==0): trust is the TOFU pin, layered by the app's
 * gmcerts above this seam; this tool just proves the transport works.
 *
 * The OT pump must YieldToAnyThread() between would-block polls so the OT
 * async notifiers run -- a tight spin starves the callbacks and the exchange
 * looks stuck (known dead end; see docs/arcana.md).  Timing is TickCount
 * (1/60 s), the only clock the console app has.
 *
 * usage: cn_ot_smoke <host> [port] [path]
 *        e.g. cn_ot_smoke 10.0.2.2 1965 /
 *
 * Output goes BOTH to stdout (someone running it from a shell on the guest)
 * and to cn_ot_smoke.log at the boot-volume root, so the result survives a
 * guest-launched run and is pullable with:
 *   ~/classic/vm/bin/retrieve-log.sh --vm macos9 cn_ot_smoke.log
 * (LaunchAPPL's socket-returned stdout is not reliable for CONSOLE binaries
 * in the macos9 guest -- see docs/arcana.md.  (0,0) = the app's default
 * volume, which is the volume it launched from; a hard-coded refnum (2) is
 * not reliable.  Each launch truncates the log fresh.)
 */
#include "classicnet/cn_ot.h"
#include "classicnet/cn_tls.h"
#include "classicnet/cn_errors.h"
#include "cn_mac_time.h"   /* cn_collect_jitter: the machine has no HW RNG */

#include <Events.h>        /* TickCount */
#include <MacTypes.h>
#include <Files.h>         /* FSMakeFSSpec, FSpCreate, FSpOpenDF, FSWrite */
#include <Threads.h>       /* YieldToAnyThread */
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SMOKE_BODY_CAP 262144    /* reply buffer: reserved once, never freed */
#define SMOKE_MAX_TICKS (45 * 60) /* ~45 s at 60 ticks/s */

/* --- boot-root log mirror (retrieve-log.sh pattern) ------------------------
 * Every stage writes a line to cn_ot_smoke.log (also fprintf'd to stdout).
 * FSMakeFSSpec(0,0,...) = the volume the app launched from; a hard-coded
 * refnum (2) is NOT reliable under a Startup Items / LaunchAPPL launch. */
static short gLogRef = -1;

static void smoke_tee(const char *s) {
    fputs(s, stdout);
    if (gLogRef < 0) {
        FSSpec spec;
        char ps[256];
        OSErr e;
        strcpy(ps + 1, "cn_ot_smoke.log");
        ps[0] = (char)strlen(ps + 1);
        e = FSMakeFSSpec(0, 0, (ConstStr255Param)ps, &spec);
        if (e != noErr && e != fnfErr) return;
        e = FSpCreate(&spec, 'TEXT', 'TEXT', 0);
        if (FSpOpenDF(&spec, fsWrPerm, &gLogRef) != noErr) { gLogRef = -1; return; }
        (void)SetEOF(gLogRef, 0);   /* fresh log each launch */
    }
    long n = (long)strlen(s);
    if (n > 0 && FSWrite(gLogRef, &n, s) == noErr)
        (void)FlushVol(0, 0);       /* survive a crash */
}

static void smoke_teef(const char *fmt, ...) {
    char buf[512];
    va_list ap;
    va_start(ap, fmt);
    (void)vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    smoke_tee(buf);
}

int main(int argc, char **argv)
{
    const char *host = argc > 1 ? argv[1] : "10.0.2.2";
    UInt16      port = (UInt16)(argc > 2 ? atoi(argv[2]) : 1965);
    const char *path = argc > 3 ? argv[3] : "/";
    CNOTTransport   tcp;
    CNTlsTransport  tls;
    CNTransport    *otT, *tlsT;
    static char     body[SMOKE_BODY_CAP];
    static char     req[1024];
    char            urlHost[64];
    OSStatus        s;
    UInt32          reqLen, sent, got, total = 0;
    Boolean         eof;
    long            t0;
    int             phase;

    /* Line-buffered stdout: evidence survives a late crash. */
    setvbuf(stdout, NULL, _IOLBF, 0);

    smoke_teef("ClassicNet M-tier (OS 9): Gemini GET over TLS + Open Transport\r\n");

    /* 1 -- Open Transport startup, then the TCP transport (DNS inside
     * CN_OTCreate; blocking, capped by OT itself). */
    if (CN_OTStartup() != noErr) { smoke_teef("cn_ot_smoke fail stage=1 err=otstartup\r\n"); return 1; }
    if (CN_OTCreate(&tcp, host, port, &otT) != noErr) {
        smoke_teef("cn_ot_smoke fail stage=1 err=create host=%s\r\n", host);
        CN_OTShutdown();
        return 1;
    }

    /* 2 -- TLS layer, VERIFY_NONE: trust is the TOFU pin, not a CA. */
    if (CN_TlsCreate(&tls, otT, host, NULL, 0, &tlsT) != noErr) {
        smoke_teef("cn_ot_smoke fail stage=2 err=tlsinit host=%s\r\n", host);
        CN_OTDispose(&tcp);
        CN_OTShutdown();
        return 1;
    }

    /* Harden the RNG seed with timing jitter (no hardware RNG on Classic). */
    { unsigned char j[32]; cn_collect_jitter(j, sizeof(j)); (void)CN_TlsAddEntropy(&tls, j, sizeof(j)); }

    /* 3a -- connect: poll until the OT layer is up (yield to let notifiers run). */
    t0 = TickCount();
    s = otT->poll(otT);
    while (s == kCNErrWouldBlock && (TickCount() - t0) < SMOKE_MAX_TICKS) {
        YieldToAnyThread();
        s = otT->poll(otT);
    }
    if (s != noErr) {
        smoke_teef("cn_ot_smoke fail stage=3a err=%ld host=%s\r\n", (long)s, host);
        CN_TlsDispose(&tls);
        CN_OTDispose(&tcp);
        CN_OTShutdown();
        return 1;
    }

    /* 3b -- handshake: poll until the TLS layer is up. */
    t0 = TickCount();
    s = tlsT->poll(tlsT);
    while (s == kCNErrWouldBlock && (TickCount() - t0) < SMOKE_MAX_TICKS) {
        YieldToAnyThread();
        s = tlsT->poll(tlsT);
    }
    if (s != noErr) {
        smoke_teef("cn_ot_smoke fail stage=3b err=%ld tlsErr=%d host=%s\r\n",
               (long)s, tls.lastError, host);
        CN_TlsDispose(&tls);
        CN_OTDispose(&tcp);
        CN_OTShutdown();
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

    t0 = TickCount();
    phase = 0;   /* 0 = sending, 1 = receiving, 2 = done */
    while (phase < 2 && (TickCount() - t0) < SMOKE_MAX_TICKS) {
        YieldToAnyThread();   /* keep OT notifiers alive every iteration */
        if (phase == 0) {
            sent = 0;
            s = tlsT->send(tlsT, req, reqLen, &sent);
            if (s != noErr) { smoke_teef("cn_ot_smoke fail stage=4 err=send host=%s\r\n", host); phase = -1; break; }
            if (sent == reqLen) phase = 1;
        } else {
            eof = false;
            got = 0;
            s = tlsT->recv(tlsT, body + total, (UInt32)(sizeof(body) - total), &got, &eof);
            if (s != noErr) { smoke_teef("cn_ot_smoke fail stage=4 err=recv host=%s\r\n", host); phase = -1; break; }
            total += got;
            if (eof) { phase = 2; break; }
            if (total >= sizeof(body)) { phase = 2; break; }
        }
    }

    CN_TlsDispose(&tls);
    CN_OTDispose(&tcp);
    CN_OTShutdown();

    if (phase != 2) {
        smoke_teef("cn_ot_smoke fail stage=4 err=incomplete phase=%d host=%s\r\n", phase, host);
        return 1;
    }

    /* Report the Gemini status head + a count; the body head is a graceful
       hint (may be binary) so cap the print and mark a NUL. */
    body[total < sizeof(body) ? total : sizeof(body) - 1] = '\0';
    smoke_teef("cn_ot_smoke ok host=%s port=%u path=%s bytes=%lu\r\n", host,
           (unsigned)port, path, (unsigned long)total);
    smoke_teef("----- body head -----\r\n%s\r\n----------------------\r\n",
           total > 0 ? body : "(empty)");
    return 0;
}
