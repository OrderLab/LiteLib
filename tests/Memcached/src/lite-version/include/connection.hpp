#pragma once

#include <event.h>

#include <array>
#include <cstdint>
#include <lite.hpp>
#include <memory>

#include "packet.hpp"
#include "service.hpp"

/// Represents a single connection from a client.
class Connection {
 public:
  Connection(const Connection&) = delete;
  Connection& operator=(const Connection&) = delete;

  /// Construct a connection with the given socket fd and add it to the specific
  /// event_base.
  using EventHandler = void (*)(evutil_socket_t, short, void*);
  explicit Connection(const evutil_socket_t sfd, const int event_flags,
                      struct event_base* base, EventHandler event_handler,
                      MemcachedLiteServer& lite_server,
                      bool is_client_connection,
                      const char* backend_addr = nullptr,
                      const char* backend_port = nullptr);

  ~Connection();

  /// Accept a new connection.
  int Accept();

  /// Handle completion of a read operation.
  bool Read();

 private:
  /// Underlying Memcached Server implementation.
  MemcachedLiteServer& lite_server_;

  /// Socket file descriptor for the client and backend.
  evutil_socket_t client_fd_, backend_fd_;

  /// The event associated with the connection.
  event client_event_, backend_event_;

  /// Buffer for incoming data.
  std::array<uint8_t, 16384> buffer_;

  /// The incoming request.
  std::unique_ptr<Packet> request_;

  SimpleParser request_parser_;

  /// Handle completion of a write operation.
  std::unique_ptr<std::vector<uint8_t>> response_buffer_;
  void FlushBuffer();

  void Write(std::unique_ptr<std::vector<uint8_t>> buffer);

  friend class MemcachedService;
};
