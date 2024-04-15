#pragma once

#include <event.h>
#include <netinet/tcp.h>

#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "core.hpp"

namespace lite {

/// Represents a single connection from a client.
template <typename Packet, typename Application, typename CacheKey,
          typename CacheEntry, typename LogEntry>
class Connection {
  using LiteCoreInstance =
      LiteCore<Application, Packet, Connection, CacheKey, CacheEntry, LogEntry>;

 public:
  Connection(const Connection&) = delete;
  Connection& operator=(const Connection&) = delete;

  /// Construct a connection with the given socket fd and add it to the specific
  /// event_base.
  using EventHandler = void (*)(evutil_socket_t, short, void*);
  explicit Connection(const evutil_socket_t sfd, const int event_flags,
                      struct event_base* base, EventHandler event_handler,
                      void* lite_server, LiteCoreInstance& lite_core,
                      bool is_client_connection)
      : base_(base),
        client_fd_(sfd),
        request_(std::make_unique<Packet>()),
        lite_server_(lite_server),
        lite_core_(lite_core),
        response_buffer_(std::make_unique<std::vector<uint8_t>>()) {
    event_set(&client_event_, sfd, event_flags, event_handler,
              static_cast<void*>(this));
    event_base_set(base, &client_event_);
    if (event_add(&client_event_, 0) == -1) {
      perror("client event_add");
      throw std::runtime_error("client event_add");
    }

    if (is_client_connection) ConnectBackend();
  }

  ~Connection() {
    /* delete the event, the socket and the conn */
    close(backend_fd_);
    close(client_fd_);
    event_del(&client_event_);
    event_del(&backend_event_);
    // std::cerr << "connection closed" << std::endl;
  }

  /// Accept a new connection.
  [[nodiscard]] int Accept() const {
    socklen_t addrlen;
    struct sockaddr_storage addr;
    addrlen = sizeof(addr);
    return accept4(client_fd_, (struct sockaddr*)&addr, &addrlen,
                   SOCK_NONBLOCK);
  }

  /// Handle completion of a read operation.
  bool Read() {
    // TODO: handle the case when the buffer is not large enough
    ssize_t bytes_transferred;
    if ((bytes_transferred = read(client_fd_, buffer_.data(), 16384)) <= 0) {
      perror("read");
      return false;
    }
    uint8_t* begin = buffer_.data();
    uint8_t* end = begin + bytes_transferred;
    while (begin != end) {
      const auto result = request_->Deserialize(begin, end);
      if (result == kGood) {
        lite_core_.Serve(std::move(request_), *this);
        request_ = std::make_unique<Packet>();
      } else if (result == kIndeterminate) {
        continue;
      } else if (result == kBad) {
        std::cerr << "failed to parse request" << std::endl;
        return false;
      }
    }
    return true;
  }

  /// Try to connect to the backend
  static evutil_socket_t TryConnectBackend(const std::string& addr,
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

  /// Try to connect to the backend and set event
  bool ConnectBackend() {
    // Set up a socket connection to the backend server
    if ((backend_fd_ = TryConnectBackend(lite_core_.backend_addr_,
                                         lite_core_.backend_port_)) == -1) {
      return false;
    }

    // Add an event that listens to the backend server's messages
    event_set(&backend_event_, backend_fd_, EV_READ | EV_PERSIST,
              Connection::BackendHandler, static_cast<void*>(this));
    event_base_set(base_, &backend_event_);
    if (event_add(&backend_event_, 0) == -1) {
      perror("backend event_add");
      throw std::runtime_error("backend event_add");
    }

    return true;
  }

  static void BackendHandler(evutil_socket_t fd, short which, void* arg_conn) {
    auto conn = static_cast<Connection*>(arg_conn);

    std::vector<uint8_t> buffer(16384);
    const ssize_t bytes_transferred =
        read(conn->backend_fd_, buffer.data(), 16384);
    if (bytes_transferred <= 0) {
      // TODO: maybe we can switch to emergency mode automatically here
      perror("read from backend");
      delete conn;
      return;
    }
    // TODO: add a hook here?
    conn->Write(conn->client_fd_, buffer, bytes_transferred);
  }

  /// Handle completion of a write operation.
  std::unique_ptr<std::vector<uint8_t>> response_buffer_;
  void FlushBuffer(const evutil_socket_t fd) {
    Write(client_fd_, std::move(response_buffer_));
    response_buffer_ = std::make_unique<std::vector<uint8_t>>();
  }

  static void Write(const evutil_socket_t fd,
                    const std::vector<uint8_t>&& buffer) {
    Write(fd, buffer, buffer.size());
  }
  static void Write(const evutil_socket_t fd,
                    const std::unique_ptr<std::vector<uint8_t>> buffer) {
    Write(fd, *buffer, buffer->size());
  }
  static void Write(const evutil_socket_t fd,
                    const std::shared_ptr<std::vector<uint8_t>> buffer) {
    Write(fd, *buffer, buffer->size());
  }
  static void Write(const evutil_socket_t fd, const std::vector<uint8_t> buffer,
                    size_t len) {
    // TODO: async?
    // TODO: use transmit() implementation in Memcached
    const uint8_t* begin = buffer.data();
    while (len) {
      ssize_t bytes_written = write(fd, begin, len);
      if (bytes_written <= 0) {
        perror("write");  // TODO: max tries
      } else {
        len -= bytes_written;
        begin += bytes_written;
      }
    }
  }

  bool is_in_transaction_ = false;
  std::vector<std::shared_ptr<Packet>> transactions_;

  /// Socket file descriptor for the client and backend.
  evutil_socket_t client_fd_, backend_fd_;

  void* lite_server_;  // LiteServer<Packet, Service>

 private:
  /// Corresponding worker's event_base
  struct event_base* const base_;

  /// Underlying Lite Core.
  LiteCoreInstance& lite_core_;

  /// The event associated with the connection.
  event client_event_, backend_event_;

  /// Buffer for incoming data.
  std::array<uint8_t, 16384> buffer_;

  /// The incoming request.
  std::unique_ptr<Packet> request_;
};

}  // namespace lite