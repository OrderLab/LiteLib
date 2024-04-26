#include "network_utils.hpp"

#include <netinet/tcp.h>

#include <cstring>
#include <iostream>

namespace lite {

namespace network {
evutil_socket_t TryConnectBackend(const std::string& addr,
                                  const std::string& port) {
  std::cerr << "Try to connect to backend\n";
  evutil_socket_t backend_fd;
  struct addrinfo hints, *res;

  memset(&hints, 0, sizeof(hints));
  hints.ai_family = AF_UNSPEC;
  hints.ai_socktype = SOCK_STREAM;

  if (getaddrinfo(addr.c_str(), port.c_str(), &hints, &res) != 0) {
    perror("getaddrinfo");
    return -1;
  }

  bool connected = false;
  int flags = 1;
  struct linger ling = {0, 0};
  for (; res; res = res->ai_next) {
    backend_fd = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    if (backend_fd == -1) {
      // perror("failed to connect backend");
      // goto connect_backend_exit;
      continue;
    }

    if (setsockopt(backend_fd, SOL_SOCKET, SO_REUSEADDR, (void*)&flags,
                   sizeof(flags)) != 0) {
      perror("failed to set SO_REUSEADDR for backend");
      continue;
    }

    if (setsockopt(backend_fd, SOL_SOCKET, SO_KEEPALIVE, (void*)&flags,
                   sizeof(flags)) != 0) {
      perror("failed to set SO_KEEPALIVE for backend");
      continue;
    }

    if (setsockopt(backend_fd, SOL_SOCKET, SO_LINGER, (void*)&ling,
                   sizeof(ling)) != 0) {
      perror("failed to set SO_LINGER for backend");
      continue;
    }

    if (setsockopt(backend_fd, IPPROTO_TCP, TCP_NODELAY, (void*)&flags,
                   sizeof(flags)) != 0) {
      perror("failed to set TCP_NODELAY for backend");
      continue;
    }

    if (connect(backend_fd, res->ai_addr, res->ai_addrlen) == -1) {
      // perror("failed to connect backend");
      // goto connect_backend_exit;
      continue;
    }

    connected = true;
    break;
  }

  if (!connected) {
    perror("failed to connect backend");
    goto connect_backend_exit;
  }

  std::cerr << "Backend connected, fd: " << backend_fd << std::endl;

  return backend_fd;

connect_backend_exit:
  freeaddrinfo(res);
  return -1;
}

bool Write(const evutil_socket_t fd, const std::vector<uint8_t> buffer,
           size_t len) {
  // TODO: async?
  // TODO: use transmit() implementation in Memcached
  const uint8_t* begin = buffer.data();
  while (len) {
    ssize_t bytes_written = write(fd, begin, len);  // BUG: it's nonblocking
    if (bytes_written <= 0) {
      perror("write");  // TODO: max tries
      return false;
    } else {
      len -= bytes_written;
      begin += bytes_written;
    }
  }
  return true;
}
bool Write(const evutil_socket_t fd, const std::vector<uint8_t>&& buffer) {
  return Write(fd, buffer, buffer.size());
}
bool Write(const evutil_socket_t fd,
           const std::unique_ptr<std::vector<uint8_t>>&& buffer) {
  return Write(fd, *buffer, buffer->size());
}
bool Write(const evutil_socket_t fd,
           const std::shared_ptr<std::vector<uint8_t>> buffer) {
  return Write(fd, *buffer, buffer->size());
}

}  // namespace network

}  // namespace lite