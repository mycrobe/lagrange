/*
 * tls_smoke -- second M-tier (Mac OS 9 PPC) network proof for lagrange:
 * a Gemini fetch THROUGH the_Foundation's iTlsRequest ClassicNet OT seam.
 *
 * This is the L9 mirror of the darwin8 d8_tls_smoke: it cross-builds the
 * the_Foundation portable core (string/block/thread/mutex/condition + the
 * classicnet tlsrequest.c OT backend) + cn_ot + cn_tls/mbedTLS-ppc into a
 * Mac OS 9 .bin and, on the macos9 guest, proves lagrange's real
 * gmrequest/gmcerts fetch path (iTlsRequest) is portable to Classic.
 *
 * TOFU-accept (verify func returns iTrue): the app's trust gate (gmcerts)
 * layers above this seam; this tool proves the transport + TLS + cert capture.
 *
 * Output goes to BOTH stdout and tls_smoke.log at the boot-volume root, so it
 * survives a Startup Items / guest-launched run (retrieve-log.sh pulls it):
 *   ~/classic/vm/bin/retrieve-log.sh --vm macos9 tls_smoke.log
 * Same pattern as cn_ot_smoke.c (LaunchAPPL-returned stdout is unreliable for
 * CONSOLE binaries; (0,0) = the app's launch volume; fresh log each launch).
 *
 * usage: tls_smoke <host> [port] [path]
 *        e.g. tls_smoke 10.0.2.2 1965 /
 */
#include "the_Foundation/tlsrequest.h"
#include "the_Foundation/string.h"

#include <Events.h>        /* TickCount */
#include <MacTypes.h>
#include <Files.h>         /* FSMakeFSSpec, FSpCreate, FSpOpenDF, FSWrite */
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* --- boot-root log mirror (retrieve-log.sh pattern; see cn_ot_smoke.c) --- */
static short gLogRef = -1;

static void smoke_tee(const char *s) {
    fputs(s, stdout);
    if (gLogRef < 0) {
        FSSpec spec;
        char ps[256];
        OSErr e;
        strcpy(ps + 1, "tls_smoke.log");
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

/* TOFU-style verify func: accept any leaf certificate (depth 0). */
static iBool onTofuVerify(iTlsRequest *req, const iTlsCertificate *cert, int depth) {
    iUnused(req, cert, depth);
    return iTrue;
}

int main(int argc, char **argv) {
    const char  *host = argc > 1 ? argv[1] : "10.0.2.2";
    uint16_t     port = (uint16_t) (argc > 2 ? atoi(argv[2]) : 1965);
    const char  *path = argc > 3 ? argv[3] : "/";

    setvbuf(stdout, NULL, _IOLBF, 0);
    smoke_teef("[tls_smoke] the_Foundation ClassicNet OT seam: Gemini GET over iTlsRequest\r\n");

    int rc = 1;
    iString hostStr, reqLine;
    initCStr_String(&hostStr, host);
    init_String(&reqLine);
    appendFormat_String(&reqLine, "gemini://%s:%u%s\r\n", host, (unsigned)port, path);

    iBlock content;
    init_Block(&content, 0);
    set_Block(&content, &reqLine.chars);

    iTlsRequest *req = new_TlsRequest();
    setHost_TlsRequest(req, &hostStr, port);
    setContent_TlsRequest(req, &content);
    submit_TlsRequest(req);
    setVerifyFunc_TlsRequest(onTofuVerify);
    waitForFinished_TlsRequest(req);

    const enum iTlsRequestStatus status = status_TlsRequest(req);
    const iTlsCertificate *cert = serverCertificate_TlsRequest(req);

    if (status == error_TlsRequestStatus) {
        smoke_teef("[tls_smoke] FAIL: request errored: %s\r\n",
                   cstr_String(errorMessage_TlsRequest(req)));
        goto done;
    }
    if (!isVerified_TlsRequest(req)) {
        smoke_teef("[tls_smoke] FAIL: request not verified\r\n");
        goto done;
    }
    if (!cert || isEmpty_TlsCertificate(cert)) {
        smoke_teef("[tls_smoke] FAIL: no server certificate\r\n");
        goto done;
    }

    iBlock *data = readAll_TlsRequest(req);
    if (!data || size_Block(data) == 0) {
        smoke_teef("[tls_smoke] FAIL: empty response body\r\n");
        delete_Block(data);
        goto done;
    }
    const char *body = constData_Block(data);
    const size_t blen = size_Block(data);
    if (blen < 2 || !(body[0] == '2' && body[1] >= '0' && body[1] <= '9')) {
        smoke_teef("[tls_smoke] FAIL: status '%.*s' not 2x\r\n",
                   (int) (blen < 20 ? blen : 20), body);
        delete_Block(data);
        goto done;
    }

    iString *subject = subject_TlsCertificate(cert);
    iBlock *fp = fingerprint_TlsCertificate(cert);
    smoke_teef("[tls_smoke] OK: status '%.*s' body=%lu certSubj='%s' isVerified=%d\r\n",
               (int) (blen < 20 ? blen : 20), body, (unsigned long) blen,
               cstr_String(subject), isVerified_TlsRequest(req));
    smoke_teef("----- body head ----\r\n%.*s\r\n---------------------\r\n",
               (int) (blen < 200 ? blen : 200), body);
    delete_String(subject);
    delete_Block(fp);
    delete_Block(data);
    rc = 0;

done:
    cancel_TlsRequest(req);
    iRelease(req);
    deinit_Block(&content);
    deinit_String(&reqLine);
    deinit_String(&hostStr);
    setVerifyFunc_TlsRequest(NULL);
    deinit_Foundation();
    return rc;
}
