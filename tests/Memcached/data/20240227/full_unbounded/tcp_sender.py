import socket
import time
import sys

server_ip = '127.0.0.1'
server_port = int(sys.argv[1:][0])

sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)

sock.connect((server_ip, server_port))

while True:
    data = 'Hello, Server!'
    print(sock.sendall(data.encode()))
    time.sleep(1)