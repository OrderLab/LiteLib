#pragma once

#include <array>
#include <cstdint>
#include <memory>

#include "core.hpp"
#include "logger.hpp"
#include "network_utils.hpp"

namespace lite {

/// Represents a single connection from a client.
template <typename Application, typename Request, typename Response,
          typename ConnectionInfo, typename CacheKey, typename CacheEntry>
class Connection {
  using ConnectionInstance = Connection<Application, Request, Response,
                                        ConnectionInfo, CacheKey, CacheEntry>;
  using LiteCoreInstance = LiteCore<Application, Request, Response,
                                    ConnectionInfo, CacheKey, CacheEntry>;
  using LoggerInstance = Logger<Application, Request, Response, ConnectionInfo,
                                CacheKey, CacheEntry>;
  using LogEntryInstance = LogEntry<Application, Request, Response,
                                    ConnectionInfo, CacheKey, CacheEntry>;
  using CacheInstance = Cache<Application, Request, Response, ConnectionInfo,
                              CacheKey, CacheEntry>;
  using CacheInnerInstance = CacheInner<Application, Request, Response,
                                        ConnectionInfo, CacheKey, CacheEntry>;
  using WorkerInstance = Worker<Application, Request, Response, ConnectionInfo,
                                CacheKey, CacheEntry>;

 public:
  Connection(const Connection&) = delete;
  Connection& operator=(const Connection&) = delete;

  /// Construct a connection with the given socket fd and add it to the specific
  /// event_base.
  using EventHandler = void (*)(evutil_socket_t, short, void*);
  explicit Connection(const evutil_socket_t sfd, const int event_flags,
                      struct event_base* base, EventHandler event_handler,
                      void* lite_server, LiteCoreInstance& lite_core,
                      bool is_client_connection, WorkerInstance* worker_ptr);

  ~Connection();

  /// Accept a new connection.
  [[nodiscard]] int Accept() const {
    socklen_t addrlen;
    struct sockaddr_storage addr;
    addrlen = sizeof(addr);
    return accept4(client_fd_, (struct sockaddr*)&addr, &addrlen,
                   SOCK_NONBLOCK);
  }

  /// Handle completion of a client read operation.
  static void ClientHandler(evutil_socket_t fd, short which, void* arg_conn);

  static void BackendHandler(evutil_socket_t fd, short which, void* arg_conn);

  void RequestUpdate(uint8_t* buffer, int len, uint32_t seq_num);

  void ResponseUpdate(uint8_t* buffer, int len, uint32_t seq_num);

  /// Try to connect to the backend and set event
  bool ConnectBackend();

  ConnectionInfo extra_app_info_;

  /// Socket file descriptor for the client and backend.
  evutil_socket_t client_fd_, backend_fd_;

  uint32_t expected_seq_num_=0;

  /// The pending requests
  ThreadSafeQueue<std::pair<std::shared_ptr<Request>, bool>> pending_requests_;

  void* lite_server_;

  /// Underlying Lite Core.
  LiteCoreInstance& lite_core_;

 private:  // ensure the order of initialization
  std::shared_ptr<ConnectionInstance*> self_;

  LogEntryInstance* log_head_;

 public:
  CacheInstance cache_;

  LoggerInstance logger_;

 private:
  /// Corresponding worker's event_base
  struct event_base* const base_;

  WorkerInstance* worker_ptr_;

  /// The event associated with the connection.
  event client_event_, backend_event_;

  /// Buffer for incoming data.
  alignas(16) uint8_t buffer_[131072];

  /// The incoming request.
  std::shared_ptr<Request> request_;

  /// The outgoing response.
  std::shared_ptr<Response> response_;
};

}  // namespace lite