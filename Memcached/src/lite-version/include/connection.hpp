#pragma once

#include <event.h>

#include <array>
#include <cstdint>
#include <memory>

#include "packet.hpp"
#include "request_handler.hpp"
#include "request_parser.hpp"

namespace memcached {
namespace server {

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
                      RequestHandler& request_handler);

  ~Connection();

  /// Accept a new connection.
  int Accept();

  /// Handle completion of a read operation.
  bool Read();

 private:
  /// Socket file descriptor.
  evutil_socket_t fd_;

  /// The event associated with the connection.
  event event_;

  /// The handler used to process the incoming request.
  RequestHandler& request_handler_;

  /// Buffer for incoming data.
  std::array<uint8_t, 16384> buffer_;

  /// The incoming request.
  Packet request_;

  /// The parser for the incoming request.
  RequestParser request_parser_;

  /// The pending responses
  std::vector<Packet> responses_;

  /// Handle completion of a write operation.
  void Write(std::unique_ptr<std::vector<uint8_t>> buffer);
};

typedef std::shared_ptr<Connection> connection_ptr;

}  // namespace server
}  // namespace memcached
