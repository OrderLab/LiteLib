import socket
import threading
import select

size_limit = 32768
server_ip = '172.16.0.2'

class RequestHandler(threading.Thread):
    def __init__(self, client_socket, redis_port, redis_lite_port):
        super().__init__()
        self.client_socket = client_socket
        self.redis_port = redis_port
        self.redis_lite_port = redis_lite_port

    def run(self):
        # Receive request from the client
        request = self.client_socket.recv(size_limit)

        # Forward the request to Redis
        redis_socket = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        redis_socket.connect((server_ip, self.redis_port))
        redis_socket.send(request)

        # Try to connect to Redis-lite
        try:
            redis_lite_socket = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            redis_lite_socket.connect((server_ip, self.redis_lite_port))
            redis_lite_socket.send(request)
        except ConnectionRefusedError:
            redis_lite_socket = None

        # Use select to wait for responses
        sockets_to_check = [redis_socket]
        if redis_lite_socket is not None:
            sockets_to_check.append(redis_lite_socket)

        ready_sockets, _, _ = select.select(sockets_to_check, [], [], 5.0)

        for sock in ready_sockets:
            response = sock.recv(size_limit)
            self.client_socket.send(response)

        # Close all sockets
        redis_socket.close()
        if redis_lite_socket is not None:
            redis_lite_socket.close()
        self.client_socket.close()


def main(client_socket, redis_port, redis_lite_port):
    # Create listening socket
    server_socket = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    server_socket.bind(('0.0.0.0', client_socket))
    server_socket.listen(5)

    while True:
        # Accept client connection
        client_socket, _ = server_socket.accept()
        handler = RequestHandler(client_socket, redis_port, redis_lite_port)
        handler.start()


if __name__ == "__main__":
    main(client_socket=6279, redis_port=6379, redis_lite_port=6479)