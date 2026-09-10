/*
 * N2 host unit/integration test: the ClassicNet-backed iTlsRequest/
 * iTlsCertificate (TLS over cn_tls / mbedTLS). Connects to a local TLS Gemini
 * test server, sends a `gemini://host/path<CRLF>` request, and asserts:
 *
 *   - the request finishes without error (status != error),
 *   - the response body is non-empty and carries a 2x Gemini status,
 *   - the server certificate is captured and non-empty,
 *   - isVerified() is true,
 *   - the certificate reports a non-empty subject/issuer and fingerprint,
 *   - verifyDomain() matches the requested host, isExpired() is false,
 *   - verify_TlsCertificate() returns authority with the pinned CA (phase 1),
 *     and something other than authority under the TOFU path (phase 2, no CA --
 *     the verify func accepts it).
 *
 * Run under ASan/UBSan. Usage:
 *   t_classicnet_tls <host> <port> <path> <ca.pem>
 * (the TLS server is started by scripts/test-classicnet-tls.sh)
 */
#include "the_Foundation/tlsrequest.h"
#include "the_Foundation/string.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int gotReadyRead = 0;
static int gotSent = 0;
static int gotFinished = 0;

static void onReadyRead(void *ctx, iTlsRequest *req) { iUnused(ctx, req); gotReadyRead++; }
static void onSent(void *ctx, iTlsRequest *req, size_t sent, size_t toSend) {
    iUnused(ctx, req, sent, toSend);
    gotSent++;
}
static void onFinished(void *ctx, iTlsRequest *req) { iUnused(ctx, req); gotFinished++; }

/* TOFU-style verify func: accept any leaf certificate (depth 0). */
static iBool onTofuVerify(iTlsRequest *req, const iTlsCertificate *cert, int depth) {
    iUnused(req, cert);
    return iTrue;
}

/* A verify func that REJECTS the leaf -- stands in for the TOFU pin/mismatch
   gate (the app's verify_GmCerts_ returns false when the pinned fingerprint does
   not match the server cert). An openSSL-style mismatch rejects cert. */
static iBool onRejectVerify(iTlsRequest *req, const iTlsCertificate *cert, int depth) {
    iUnused(req, cert);
    return iFalse;
}

/* Assert a single completed request against the running TLS server. */
static int runRequest(const char *host, uint16_t port, const char *path, const char *caPath,
                      iBool expectAuthority, const char *tag) {
    int rc = 1;
    gotReadyRead = 0;
    gotSent = 0;
    gotFinished = 0;

    iTlsRequest *req = new_TlsRequest();
    iConnect(TlsRequest, req, readyRead, req, onReadyRead);
    iConnect(TlsRequest, req, sent, req, onSent);
    iConnect(TlsRequest, req, finished, req, onFinished);

    /* Configure (or clear) the CA bundle. */
    iString ca;
    init_String(&ca);
    if (caPath) {
        setCStr_String(&ca, caPath);
    }
    setCACertificates_TlsRequest(&ca, &ca);
    deinit_String(&ca);

    iString hostStr;
    initCStr_String(&hostStr, host);

    iString reqLine;
    init_String(&reqLine);
    appendFormat_String(&reqLine, "gemini://%s%s\r\n", host, path);

    iBlock content;
    init_Block(&content, 0);
    set_Block(&content, &reqLine.chars);

    setHost_TlsRequest(req, &hostStr, port);
    setContent_TlsRequest(req, &content);
    submit_TlsRequest(req);
    waitForFinished_TlsRequest(req);

    const enum iTlsRequestStatus status = status_TlsRequest(req);
    const iTlsCertificate *cert = serverCertificate_TlsRequest(req);

    if (status == error_TlsRequestStatus) {
        printf("[%s] FAIL: request errored: %s\n", tag, cstr_String(errorMessage_TlsRequest(req)));
        goto done;
    }
    if (!isVerified_TlsRequest(req)) {
        printf("[%s] FAIL: request not verified\n", tag);
        goto done;
    }
    if (!cert || isEmpty_TlsCertificate(cert)) {
        printf("[%s] FAIL: no server certificate\n", tag);
        goto done;
    }

    iBlock *data = readAll_TlsRequest(req);
    if (!data || size_Block(data) == 0) {
        printf("[%s] FAIL: empty response body\n", tag);
        delete_Block(data);
        goto done;
    }
    const char *body = constData_Block(data);
    const size_t blen = size_Block(data);
    if (blen < 2 || !(body[0] == '2' && body[1] >= '0' && body[1] <= '9')) {
        printf("[%s] FAIL: status '%.*s' not 2x\n", tag, (int) (blen < 20 ? blen : 20), body);
        delete_Block(data);
        goto done;
    }

    if (isExpired_TlsCertificate(cert)) {
        printf("[%s] FAIL: certificate reported expired\n", tag);
        delete_Block(data);
        goto done;
    }
    const iRangecc hostRange = range_String(&hostStr);
    if (!verifyDomain_TlsCertificate(cert, hostRange)) {
        printf("[%s] FAIL: verifyDomain did not match host '%s'\n", tag, host);
        delete_Block(data);
        goto done;
    }

    const enum iTlsCertificateVerifyStatus vstat = verify_TlsCertificate(cert);
    if (expectAuthority && vstat != authority_TlsCertificateVerifyStatus) {
        printf("[%s] FAIL: expected CA-authority cert, got verification status %d\n", tag, vstat);
        delete_Block(data);
        goto done;
    }
    if (!expectAuthority && vstat == authority_TlsCertificateVerifyStatus) {
        printf("[%s] FAIL: expected non-authority (TOFU) cert, got %d\n", tag, vstat);
        delete_Block(data);
        goto done;
    }

    iBlock *fp = fingerprint_TlsCertificate(cert);
    if (size_Block(fp) == 0) {
        printf("[%s] FAIL: empty certificate fingerprint\n", tag);
        delete_Block(fp);
        delete_Block(data);
        goto done;
    }
    delete_Block(fp);

    iString *subject = subject_TlsCertificate(cert);
    iString *issuer = issuer_TlsCertificate(cert);
    if (isEmpty_String(subject) || isEmpty_String(issuer)) {
        printf("[%s] FAIL: empty subject/issuer (sub='%s' iss='%s')\n",
               tag, cstr_String(subject), cstr_String(issuer));
        delete_String(subject);
        delete_String(issuer);
        delete_Block(data);
        goto done;
    }
    delete_String(subject);
    delete_String(issuer);

    printf("[%s] OK: status '%c%c' body=%zu bytes readyRead=%d sent=%d finished=%d "
           "verifystatus=%d isVerified=%d\n",
           tag, body[0], body[1], blen, gotReadyRead, gotSent, gotFinished,
           vstat, isVerified_TlsRequest(req));
    delete_Block(data);
    rc = 0;

done:
    deinit_String(&reqLine);
    deinit_Block(&content);
    deinit_String(&hostStr);
    cancel_TlsRequest(req);
    iRelease(req);
    return rc;
}

/* Assert the (2b) self-signed certificate generation actually produces a usable
   cert (not the empty stub): non-empty, has a private key, correct subject CN,
   not expired, self-signed verification status, non-empty PEM + fingerprints. */
static int runSelfSigned(void) {
    int rc = 1;
    iString cn, uid, email, domain, org, country;
    initCStr_String(&cn, "Alice");
    initCStr_String(&uid, "alice");
    initCStr_String(&email, "alice@example.com");
    initCStr_String(&domain, "example.com");
    initCStr_String(&org, "Example Org");
    initCStr_String(&country, "US");

    const iTlsCertificateName names[] = {
        { issuerCommonName_TlsCertificateNameType,    &cn },
        { issuerEmailAddress_TlsCertificateNameType,  &email },
        { issuerUserId_TlsCertificateNameType,        &uid },
        { issuerDomain_TlsCertificateNameType,        &domain },
        { issuerOrganization_TlsCertificateNameType,  &org },
        { issuerCountry_TlsCertificateNameType,       &country },
        { subjectCommonName_TlsCertificateNameType,   &cn },
        { subjectEmailAddress_TlsCertificateNameType, &email },
        { subjectUserId_TlsCertificateNameType,       &uid },
        { subjectDomain_TlsCertificateNameType,       &domain },
        { subjectOrganization_TlsCertificateNameType, &org },
        { subjectCountry_TlsCertificateNameType,      &country },
        { 0, NULL }
    };

    iDate until;
    initCurrent_Date(&until);
    until.year += 1; /* one year from now */

    iTlsCertificate *cert = newSelfSignedRSA_TlsCertificate(2048, until, names);
    if (!cert || isEmpty_TlsCertificate(cert)) {
        printf("[selfgen] FAIL: empty certificate\n");
        goto done;
    }
    if (!hasPrivateKey_TlsCertificate(cert)) {
        printf("[selfgen] FAIL: no private key\n");
        delete_TlsCertificate(cert);
        goto done;
    }

    iString *subject = subject_TlsCertificate(cert);
    if (isEmpty_String(subject) || !startsWith_String(subject, "CN = ")) {
        printf("[selfgen] FAIL: bad subject '%s'\n", cstr_String(subject));
        delete_String(subject);
        delete_TlsCertificate(cert);
        goto done;
    }
    delete_String(subject);

    if (isExpired_TlsCertificate(cert)) {
        printf("[selfgen] FAIL: certificate expired\n");
        delete_TlsCertificate(cert);
        goto done;
    }
    if (verify_TlsCertificate(cert) != selfSigned_TlsCertificateVerifyStatus) {
        printf("[selfgen] FAIL: expected self-signed (got %d)\n", verify_TlsCertificate(cert));
        delete_TlsCertificate(cert);
        goto done;
    }

    iBlock *fp = fingerprint_TlsCertificate(cert);
    iBlock *pkey = privateKeyFingerprint_TlsCertificate(cert);
    iString *pem = pem_TlsCertificate(cert);
    iString *keyPem = privateKeyPem_TlsCertificate(cert);
    if (size_Block(fp) == 0 || size_Block(pkey) == 0 ||
        isEmpty_String(pem) || isEmpty_String(keyPem)) {
        printf("[selfgen] FAIL: missing pem/fingerprint\n");
        delete_Block(fp);
        delete_Block(pkey);
        delete_String(pem);
        delete_String(keyPem);
        delete_TlsCertificate(cert);
        goto done;
    }
    delete_Block(fp);
    delete_Block(pkey);
    delete_String(pem);
    delete_String(keyPem);

    printf("[selfgen] OK: self-signed cert generated (subject CN, key present, not expired)\n");
    delete_TlsCertificate(cert);
    rc = 0;

done:
    deinit_String(&cn);
    deinit_String(&uid);
    deinit_String(&email);
    deinit_String(&domain);
    deinit_String(&org);
    deinit_String(&country);
    return rc;
}

/* Assert the TOFU pin/mismatch gate: with the app verify func REJECTING the leaf
   the handshake aborts, so the request reports status==error, isVerified()==false,
   and (it is a rejection of the certificate, not a network error) still carries a
   server certificate the UI can point at. */
static int runRejectGate(const char *host, uint16_t port, const char *path) {
    int rc = 1;
    iTlsRequest *req = new_TlsRequest();
    iConnect(TlsRequest, req, finished, req, onFinished);

    iString ca;
    init_String(&ca);
    setCACertificates_TlsRequest(&ca, &ca); /* no CA -> chain fails -> verify func consulted */
    deinit_String(&ca);

    iString hostStr;
    initCStr_String(&hostStr, host);
    iString reqLine;
    init_String(&reqLine);
    appendFormat_String(&reqLine, "gemini://%s%s\r\n", host, path);
    iBlock content;
    init_Block(&content, 0);
    set_Block(&content, &reqLine.chars);

    setHost_TlsRequest(req, &hostStr, port);
    setContent_TlsRequest(req, &content);
    submit_TlsRequest(req);
    waitForFinished_TlsRequest(req);

    const enum iTlsRequestStatus status = status_TlsRequest(req);
    const iTlsCertificate *cert = serverCertificate_TlsRequest(req);
    if (status != error_TlsRequestStatus) {
        printf("[reject] FAIL: expected error status, got %d\n", status);
        goto done;
    }
    if (isVerified_TlsRequest(req)) {
        printf("[reject] FAIL: rejected cert reported as verified\n");
        goto done;
    }
    if (!cert || isEmpty_TlsCertificate(cert)) {
        printf("[reject] FAIL: expected the rejected server certificate to be available\n");
        goto done;
    }
    printf("[reject] OK: mismatch gate rejected the cert (status=error, isVerified=0, cert present)\n");
    rc = 0;

done:
    deinit_String(&reqLine);
    deinit_Block(&content);
    deinit_String(&hostStr);
    cancel_TlsRequest(req);
    iRelease(req);
    return rc;
}

int main(int argc, char **argv) {
    init_Foundation();
    const char *host   = argc > 1 ? argv[1] : "localhost";
    const uint16_t port = (uint16_t) (argc > 2 ? atoi(argv[2]) : 1965);
    const char *path   = argc > 3 ? argv[3] : "/";
    const char *caPath = argc > 4 ? argv[4] : NULL;

    setVerifyFunc_TlsRequest(onTofuVerify);

    int rc = 0;
    /* Phase 1: CA-verified (server cert chained to caPath). */
    if (runRequest(host, port, path, caPath, iTrue, "ca") != 0) {
        rc = 1;
    }
    /* Phase 2: TOFU path -- no CA bundle, verify func accepts the leaf. */
    if (runRequest(host, port, path, NULL, iFalse, "tofu") != 0) {
        rc = 1;
    }
    /* Phase 3: (2b) self-signed certificate generation over mbedTLS. */
    if (runSelfSigned() != 0) {
        rc = 1;
    }
    /* Phase 4: TOFU pin/mismatch gate -- the app verify func REJECTS the leaf,
       so the handshake aborts; the request must error and report !isVerified. */
    setVerifyFunc_TlsRequest(onRejectVerify);
    if (runRejectGate(host, port, path) != 0) {
        rc = 1;
    }
    setVerifyFunc_TlsRequest(onTofuVerify);

    setVerifyFunc_TlsRequest(NULL);
    deinit_Foundation();
    return rc;
}
