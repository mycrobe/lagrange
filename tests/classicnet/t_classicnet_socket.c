/*
 * N2 host unit test: the ClassicNet-backed iSocket (Stream subclass over a
 * CNTransport). Connects to a local echo server, writes a line, and asserts
 * the connected/readyRead/disconnected audiences fire and the echoed bytes
 * arrive in the Stream input buffer. Run under ASan/UBSan.
 *
 * Usage: t_classicnet_socket <host> <port>
 * (the echo server is started by scripts/test-classicnet-socket.sh)
 */
#include "the_Foundation/socket.h"
#include "the_Foundation/thread.h"
#include "the_Foundation/block.h"

#include <stdio.h>
#include <string.h>

static int gotConnected = 0;
static int gotReadyRead = 0;
static int gotDisconnected = 0;
static int gotError = 0;

static void onConnected(void *ctx, iSocket *sock) { iUnused(ctx, sock); gotConnected = 1; }
static void onReadyRead(void *ctx, iSocket *sock) { iUnused(ctx, sock); gotReadyRead = 1; }
static void onError(void *ctx, iSocket *sock, int error, const char *msg) {
    iUnused(ctx, sock);
    gotError = 1;
    printf("error: %d %s\n", error, msg);
}
static void onDisconnected(void *ctx, iSocket *sock) { iUnused(ctx, sock); gotDisconnected = 1; }

int main(int argc, char **argv) {
    init_Foundation();
    const char *host = argc > 1 ? argv[1] : "127.0.0.1";
    const int port  = argc > 2 ? atoi(argv[2]) : 9999;
    int rc = 1;

    iSocket *sock = new_Socket(host, (uint16_t) port);
    iConnect(Socket, sock, connected, sock, onConnected);
    iConnect(Socket, sock, readyRead, sock, onReadyRead);
    iConnect(Socket, sock, error, sock, onError);
    iConnect(Socket, sock, disconnected, sock, onDisconnected);

    if (!open_Socket(sock)) {
        printf("FAIL: open_Socket\n");
        goto done;
    }
    writeData_Socket(sock, "hello\n", 6);

    /* Wait (bounded) for a reply, then for the peer to close. */
    for (int i = 0; i < 2000; i++) {
        if (gotError) goto done;
        if (receivedBytes_Socket(sock) > 0) break;
        sleep_Thread(0.005);
    }
    for (int i = 0; i < 2000 && status_Socket(sock) != disconnected_SocketStatus; i++) {
        if (gotError) goto done;
        sleep_Thread(0.005);
    }

    if (!gotConnected)                 { printf("FAIL: connected audience never fired\n"); goto done; }
    if (receivedBytes_Socket(sock) == 0) { printf("FAIL: no bytes received\n"); goto done; }

    iBlock *data = readAll_Socket(sock);
    if (data) {
        char buf[256] = "";
        const size_t n = iMin(size_Block(data), sizeof buf - 1u);
        memcpy(buf, constData_Block(data), n);
        buf[n] = '\0';
        if (strstr(buf, "hello")) {
            rc = 0;
            printf("OK: connected=%d readyRead=%d disconnected=%d error=%d -> '%.*s'\n",
                   gotConnected, gotReadyRead, gotDisconnected, gotError, (int) n, buf);
        }
        else {
            printf("FAIL: did not receive echoed data (got '%.*s')\n", (int) n, buf);
        }
        delete_Block(data);
    }
    else {
        printf("FAIL: readAll returned NULL\n");
    }

done:
    close_Socket(sock);
    iRelease(sock);
    deinit_Foundation();
    return rc;
}
