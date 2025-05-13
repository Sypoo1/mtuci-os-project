#!/usr/bin/env python3
import socket
import sys
import time

SERVER = '127.0.0.1'
PORT   = 8002
BUFF   = 4096

if len(sys.argv) != 2:
    print(f"Usage: {sys.argv[0]} <remote-file-path>")
    sys.exit(1)

filename = "DISCONNECT" + "\n"


start_time = time.time()

with socket.create_connection((SERVER, PORT)) as sock:
    sock.sendall(filename.encode())
    while True:
        data = sock.recv(BUFF)
        if not data:
            break
        sys.stdout.buffer.write(data)

end_time = time.time()


execution_time = end_time - start_time
print(f"Execution time: {execution_time:.5f} seconds")
