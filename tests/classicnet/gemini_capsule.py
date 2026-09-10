#!/usr/bin/env python3
"""
N3 -- local Gemini capsule for interactive/reproducible testing of the classicnet
seam in the canvas app (canvaswin/canvasapp). A small persistent Gemini TLS
server: serves a couple of linked pages and validates that a request was received
(so you can see the app talk to it over ClassicNet).

Usage:
  gemini_capsule.py <cert.pem> <key.pem> <port>

Reads `gemini://host/path<CRLF>`, responds with a 2x text/gemini page whose body
echoes the requested path, and logs each request to stdout. Responds then closes.
"""
import socket
import ssl
import sys
import time

PAGES = {
    "/": "\r\n# Welcome to the ClassicNet test capsule\r\n\r\n"
         "This page was fetched by the app over ClassicNet "
         "(cn_darwin8 + cn_tls/mbedTLS).\r\n\r\n"
         "=> /about About this capsule\r\n"
         "=> gemini://gemini.circumlunar.space/ A real capsule on the Gemini network\r\n",
    "/about": "\r\n# About\r\n\r\n"
              "A throwaway TLS server used to prove the lagrange classicnet seam "
              "fetches a Gemini page end to end.\r\n\r\n"
              "=> / Back\r\n",
}


def main():
    cert, key, port = sys.argv[1], sys.argv[2], int(sys.argv[3])
    ctx = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
    ctx.minimum_version = ssl.TLSVersion.TLSv1_2
    ctx.maximum_version = ssl.TLSVersion.TLSv1_2
    ctx.load_cert_chain(certfile=cert, keyfile=key)

    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as srv:
        srv.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        srv.bind(("127.0.0.1", port))
        srv.listen(8)
        sys.stdout.write("ready\n")
        sys.stdout.flush()
        while True:
            conn, _ = srv.accept()
            with conn:
                try:
                    with ctx.wrap_socket(conn, server_side=True) as tls:
                        tls.settimeout(10)
                        req = bytearray()
                        while b"\r\n" not in req:
                            chunk = tls.recv(4096)
                            if not chunk:
                                break
                            req += chunk
                        line = bytes(req).split(b"\r\n", 1)[0].decode("ascii", "replace")
                        sys.stdout.write("%s request: %s\n" % (time.strftime("%H:%M:%S"), line))
                        sys.stdout.flush()
                        # Gemini URL -> path
                        path = "/"
                        if line.startswith("gemini://"):
                            after = line[len("gemini://"):]
                            slash = after.find("/")
                            path = after[slash:] if slash >= 0 else "/"
                        # strip query/fragment
                        path = path.split("?")[0].split("#")[0]
                        body = PAGES.get(path, PAGES["/"])
                        if "gemini://gemini.circumlunar.space" in line and path == "/":
                            pass
                        tls.sendall(("20 text/gemini\r\n" + body).encode("ascii"))
                except ssl.SSLError:
                    pass
                except (ConnectionError, UnicodeDecodeError):
                    pass


if __name__ == "__main__":
    main()
