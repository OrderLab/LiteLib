#pragma once

#include <event.h>
#include <netinet/tcp.h>

#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "core.hpp"
#include "logger.hpp"
#include "network_utils.hpp"

namespace lite {

/// Represents a single connection from a client.
template <typename Request, typename Response, typename Application,
          typename CacheKey, typename CacheEntry, typename LogEntry,
          typename ConnectionInfo>
class Connection {
  using LiteCoreInstance =
      LiteCore<Application, Request, Response, ConnectionInfo, CacheKey,
               CacheEntry, LogEntry>;
  using LoggerInstance = Logger<LogEntry>;

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
        backend_fd_(std::make_shared<evutil_socket_t>(-1)),
        request_(std::make_unique<Request>()),
        response_(std::make_unique<Response>()),
        lite_server_(lite_server),
        lite_core_(lite_core) {
    event_set(&client_event_, sfd, event_flags, event_handler,
              static_cast<void*>(this));
    event_base_set(base, &client_event_);
    if (event_add(&client_event_, 0) == -1) {
      perror("client event_add");
      throw std::runtime_error("client event_add");
    }

    log_head_.backend_fd = backend_fd_;

    if (is_client_connection &&
        (!lite_core_.emergency_mode_ && !lite_core_.is_replaying_))
      ConnectBackend();
  }

  ~Connection() {
    lite_core_.live_connections_.erase(this);
    *backend_fd_ = -1;

    /* delete the event, the socket and the conn */
    close(*backend_fd_);
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

  /// Handle completion of a client read operation.
  static void ClientHandler(evutil_socket_t fd, short which, void* arg_conn) {
    auto conn = static_cast<Connection*>(arg_conn);
    if (fd != conn->client_fd_) {
      std::cerr << "ClientHandler: fd mismatch. Expecting " << conn->client_fd_
                << " but got " << fd << std::endl;
      return;
    }
    // TODO: handle the case when the buffer is not large enough
    // TODO: above TODOs apply to BackendHandler as well
    ssize_t bytes_transferred;
    if ((bytes_transferred = read(fd, conn->buffer_.data(), 16384)) <= 0) {
      if (bytes_transferred == 0)
        std::cerr << "Client disconnected: " << fd << std::endl;
      else
        perror("read from client");
      delete conn;
      // TODO: how to properly handle the case when the client disconnects as
      // expected? (e.g. quit command in Memcached)
      return;
    }
    uint8_t* begin = conn->buffer_.data();
    uint8_t* end = begin + bytes_transferred;
    while (begin != end) {
      const auto result = conn->request_->Deserialize(begin, end);
      if (result == kGood) {
        if (conn->backend_fd_ <= 0 && !conn->lite_core_.emergency_mode_) {
          conn->ConnectBackend();
        }
        if (!conn->lite_core_.HandleRequest(
                std::move(conn->request_), conn->extra_app_info_,
                conn->pending_requests_, conn->client_fd_, *conn->backend_fd_,
                conn->log_head_)) {
          delete conn;
          return;
        }
        conn->request_ = std::make_unique<Request>();
      } else if (result == kIndeterminate) {
        continue;
      } else if (result == kBad) {
        std::cerr << "failed to parse request" << std::endl;
        return;
      }
    }
    return;
  }

  static void BackendHandler(evutil_socket_t fd, short which, void* arg_conn) {
    auto conn = static_cast<Connection*>(arg_conn);
    if (fd != *conn->backend_fd_) {
      std::cerr << "BackendHandler: fd mismatch. Expecting "
                << conn->backend_fd_ << " but got " << fd << std::endl;
      return;
    }

    ssize_t bytes_transferred;
    if ((bytes_transferred = read(fd, conn->buffer_.data(), 16384)) <= 0) {
      if (bytes_transferred == 0) {
        std::cerr << "Backend disconnected: " << fd << std::endl;
        close(fd);
        *conn->backend_fd_ = -1;
      } else {
        perror("read from backend");
        delete conn;
      }
      return;
    }
    uint8_t* begin = conn->buffer_.data();
    uint8_t* end = begin + bytes_transferred;
    while (begin != end) {
      const auto result = conn->response_->Deserialize(begin, end);
      if (result == kGood) {
        if (!conn->lite_core_.HandleResponse(
                std::move(conn->response_), conn->extra_app_info_,
                conn->pending_requests_, conn->client_fd_)) {
          delete conn;
          return;
        }
        conn->response_ = std::make_unique<Response>();
      } else if (result == kIndeterminate) {
        continue;
      } else if (result == kBad) {
        std::cerr << "failed to parse response" << std::endl;
        return;
      }
    }
    return;
  }

  /// Try to connect to the backend and set event
  bool ConnectBackend() {
    // Set up a socket connection to the backend server
    if ((*backend_fd_ = network::TryConnectBackend(
             lite_core_.backend_addr_, lite_core_.backend_port_)) == -1) {
      return false;
    }

    // Add an event that listens to the backend server's messages
    event_set(&backend_event_, *backend_fd_, EV_READ | EV_PERSIST,
              Connection::BackendHandler, static_cast<void*>(this));
    event_base_set(base_, &backend_event_);
    if (event_add(&backend_event_, 0) == -1) {
      perror("backend event_add");
      throw std::runtime_error("backend event_add");
    }

    return true;
  }
  ConnectionInfo extra_app_info_;

  /// Socket file descriptor for the client and backend.
  evutil_socket_t client_fd_;
  std::shared_ptr<evutil_socket_t> backend_fd_;

  LoggerInstance::LogEntry log_head_;

  void* lite_server_;

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
  std::shared_ptr<Request> request_;

  /// The outgoing response.
  std::shared_ptr<Response> response_;

  /// The pending requests
  std::deque<std::shared_ptr<Request>> pending_requests_;
};

}  // namespace lite