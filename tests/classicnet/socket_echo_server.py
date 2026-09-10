#!/usr/bin/env python3
"""N2 host unit test -- loopback TCP echo server for the ClassicNet iSocket test.

Reads one message from a client and echoes it back prefixed with "echo: ",
then closes. Prints "ready" once listening so the runner knows to start the
client. Usage: socket_echo_server.py <port>
"""
import socket
import sys


def main():
    port = int(sys.argv[1])
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as srv:
        srv.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        srv.bind(("127.0.0.1", port))
        srv.listen(8)
        sys.stdout.write("ready\n")
        sys.stdout.flush()
        while True:
            conn, _ = srv.accept()
            with conn:
                data = conn.recv(4096)
                conn.sendall(b"echo: " + data)


if __name__ == "__main__":
    main()
