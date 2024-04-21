#pragma once

#include "cache.hpp"
#include "concept.hpp"
#include "daemon.hpp"
#include "logger.hpp"
#include "network_utils.hpp"

namespace lite {

template <typename Application, typename Request, typename Response,
          typename ConnectionInfo, typename CacheKey, typename CacheEntry,
          typename LogEntry>
  requires IsApplication<Application, Request, Response, ConnectionInfo,
                         CacheKey, CacheEntry, LogEntry>
class LiteCore : public Daemon {
 public:
  LiteCore(Application &app, const size_t &max_item_count,
           std::string &backend_addr, std::string &backend_port,
           const char pipe_path[])
      : Daemon([&] { Replay(); }, backend_port, pipe_path),
        app_(app),
        cache_(max_item_count),
        backend_addr_(backend_addr),
        backend_port_(backend_port) {}

  void HandleRequest(std::shared_ptr<Request> req, ConnectionInfo &conn_info,
                     std::deque<std::shared_ptr<Request>> &pending_requests,
                     const evutil_socket_t client_fd,
                     const evutil_socket_t backend_fd) {
    if (!emergency_mode_ && backend_fd <= 0) {
      std::cerr << "Fallback to emergency mode" << std::endl;
      emergency_mode_ = true;
    }

    if (emergency_mode_) {
      auto packet =
          app_.EmergencyServe(std::move(req), conn_info, cache_, logger_);
      const auto buffer = packet.Serialize();
      network::Write(client_fd, buffer);
    } else {
      const auto buffer = req->Serialize();
      network::Write(backend_fd, buffer);
      // TODO: enable application to filter/modify requests before pushing back
      pending_requests.push_back(req);
    }
  }

  void HandleResponse(std::shared_ptr<Response> resp, ConnectionInfo &conn_info,
                      std::deque<std::shared_ptr<Request>> &pending_requests,
                      const evutil_socket_t client_fd) {
    if (emergency_mode_) {
      std::cerr << "Trying to handle a response in emergency mode" << std::endl;
      return;
    }

    const auto buffer = resp->Serialize();
    network::Write(client_fd, buffer);

    // TODO: in parallel with network::Write MSG_DONTWAIT? O_NONBLOCK?
    const auto related_stateful_request =
        app_.Filter(resp, conn_info, pending_requests);
    if (related_stateful_request.has_value()) {
      app_.NormalUpdate(resp, std::move(related_stateful_request.value()),
                        conn_info, cache_);
    }
  }

  std::string &backend_addr_, &backend_port_;

 private:
  Application &app_;

  Cache<CacheKey, CacheEntry> cache_;

  Logger<LogEntry> logger_;

  void Replay() {
    int backend_fd, tries = 0;
    while ((backend_fd = network::TryConnectBackend(backend_addr_,
                                                    backend_port_)) == -1) {
      if (tries++ > 100) {
        std::cerr << "Replay failed to connect to backend\n";
        return;
      }
    }
    std::cerr << "Replay connected to backend in " << tries << " tries\n";
    size_t cnt = 0;
    LogEntry entry;
    while (logger_.Pop(entry)) {
      cnt++;
      const auto buffer = entry.ToRequests();
      network::Write(backend_fd, buffer);  // TODO: less writes
    }
    // TODO: in-flight requests after this?
    close(backend_fd);
    std::cerr << "Replay " << cnt << " items\n";
  }
};

}  // namespace lite
