#pragma once

#include <event.h>
#include <netinet/tcp.h>

#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "core.hpp"
#include "network_utils.hpp"

namespace lite {

/// Represents a single connection from a client.
template <typename Packet, typename Application, typename CacheKey,
          typename CacheEntry, typename LogEntry, typename ConnectionInfo>
class Connection {
  using LiteCoreInstance = LiteCore<Application, Packet, ConnectionInfo,
                                    CacheKey, CacheEntry, LogEntry>;

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
        lite_core_(lite_core) {
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
        if (backend_fd_ <= 0 && !lite_core_.emergency_mode_) {
          ConnectBackend();
        }
        lite_core_.Serve(std::move(request_), extra_app_info_, client_fd_,
                         backend_fd_);
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

  /// Try to connect to the backend and set event
  bool ConnectBackend() {
    // Set up a socket connection to the backend server
    if ((backend_fd_ = network::TryConnectBackend(
             lite_core_.backend_addr_, lite_core_.backend_port_)) == -1) {
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
    network::Write(conn->client_fd_, buffer, bytes_transferred);
  }

  ConnectionInfo extra_app_info_;

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