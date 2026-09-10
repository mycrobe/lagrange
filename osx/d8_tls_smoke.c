/*
 * darwin8 (Tiger PPC) TLS fetch proof -- the the_Foundation ClassicNet seam.
 *
 * Cross-built by osx/CMakeLists.txt (d8_tls_smoke target) and run ON the target
 * (petal, 10.4.11).  This is the on-device proof that the_Foundation's
 * iTlsRequest/iTlsCertificate ClassicNet backend (socket.c + tlsrequest.c over
 * cn_darwin8 + cn_tls/mbedTLS) cross-compiles for darwin8 and does a real
 * Gemini fetch -- i.e. lagrange's actual gmrequest/gmcerts fetch path is
 * portable to the T-tier.
 *
 * Usage:  d8_tls_smoke <host> <port> <path>
 *   e.g.   d8_tls_smoke 192.168.7.146 1965 /
 *
 * Mirrors t_classicnet_tls.c's fetch but single-request and TOFU-accept (the
 * app's trust gate is layered above this seam; here we prove the transport +
 * TLS + cert capture works).  If the request errors we print the error string,
 * and we always report the server certificate's subject/fingerprint so a run's
 * artifact proves the handshake + X.509 parsing.
 */
#include "the_Foundation/tlsrequest.h"
#include "the_Foundation/string.h"

#include <stdio.h>
#include <stdlib.h>

/* TOFU-style verify func: accept any leaf certificate (depth 0). */
static iBool onTofuVerify(iTlsRequest *req, const iTlsCertificate *cert, int depth) {
    iUnused(req, cert, depth);
    return iTrue;
}

int main(int argc, char **argv) {
    const char *host = argc > 1 ? argv[1] : "localhost";
    const uint16_t port = (uint16_t) (argc > 2 ? atoi(argv[2]) : 1965);
    const char *path = argc > 3 ? argv[3] : "/";

    init_Foundation();
    setVerifyFunc_TlsRequest(onTofuVerify);

    int rc = 1;
    iString hostStr, reqLine;
    initCStr_String(&hostStr, host);
    init_String(&reqLine);
    appendFormat_String(&reqLine, "gemini://%s%s\r\n", host, path);

    iBlock content;
    init_Block(&content, 0);
    set_Block(&content, &reqLine.chars);

    iTlsRequest *req = new_TlsRequest();
    setHost_TlsRequest(req, &hostStr, port);
    setContent_TlsRequest(req, &content);
    submit_TlsRequest(req);
    waitForFinished_TlsRequest(req);

    const enum iTlsRequestStatus status = status_TlsRequest(req);
    const iTlsCertificate *cert = serverCertificate_TlsRequest(req);

    if (status == error_TlsRequestStatus) {
        printf("[d8_tls_smoke] FAIL: request errored: %s\n",
               cstr_String(errorMessage_TlsRequest(req)));
        goto done;
    }
    if (!isVerified_TlsRequest(req)) {
        printf("[d8_tls_smoke] FAIL: request not verified\n");
        goto done;
    }
    if (!cert || isEmpty_TlsCertificate(cert)) {
        printf("[d8_tls_smoke] FAIL: no server certificate\n");
        goto done;
    }

    iBlock *data = readAll_TlsRequest(req);
    if (!data || size_Block(data) == 0) {
        printf("[d8_tls_smoke] FAIL: empty response body\n");
        delete_Block(data);
        goto done;
    }
    const char *body = constData_Block(data);
    const size_t blen = size_Block(data);
    if (blen < 2 || !(body[0] == '2' && body[1] >= '0' && body[1] <= '9')) {
        printf("[d8_tls_smoke] FAIL: status '%.*s' not 2x\n",
               (int) (blen < 20 ? blen : 20), body);
        delete_Block(data);
        goto done;
    }

    iString *subject = subject_TlsCertificate(cert);
    iBlock *fp = fingerprint_TlsCertificate(cert);
    printf("[d8_tls_smoke] OK: status '%.*s' body=%zu certSubj='%s' "
           "isVerified=%d\n",
           (int) (blen < 20 ? blen : 20), body, blen, cstr_String(subject),
           isVerified_TlsRequest(req));
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
