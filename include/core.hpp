#pragma once

#include "cache.hpp"
#include "concept.hpp"
#include "daemon.hpp"
#include "logger.hpp"
#include "network_utils.hpp"

namespace lite {

template <typename Application, typename Packet, typename ConnectionInfo,
          typename CacheKey, typename CacheEntry, typename LogEntry>
  requires IsApplication<Application, Packet, ConnectionInfo, CacheKey,
                         CacheEntry, LogEntry>
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

  void Serve(std::shared_ptr<Packet> p, ConnectionInfo &conn_info,
             const evutil_socket_t client_fd,
             const evutil_socket_t backend_fd) {
    if (emergency_mode_) {
      auto packet =
          app_.EmergencyServe(std::move(p), conn_info, cache_, logger_);
      const auto buffer = packet.Serialize();
      network::Write(client_fd, buffer);
    } else {
      if (app_.Filter(p, conn_info)) {
        app_.NormalUpdate(p, conn_info, cache_);
      }

      // Forward
      if (backend_fd <= 0) {
        std::cerr << "Fallback to emergency mode" << std::endl;
        emergency_mode_ = true;
        app_.EmergencyServe(std::move(p), conn_info, cache_, logger_);
        const auto buffer = p->Serialize();
        network::Write(client_fd, buffer);
      } else {
        const auto buffer = p->Serialize();
        network::Write(backend_fd, buffer);
      }
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
      const auto buffer = entry.ToPacket();
      network::Write(backend_fd, buffer);  // TODO: less writes
    }
    // TODO: in-flight requests after this?
    close(backend_fd);
    std::cerr << "Replay " << cnt << " items\n";
  }
};

}  // namespace lite
