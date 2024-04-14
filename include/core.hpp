#pragma once

#include "cache.hpp"
#include "concept.hpp"
#include "daemon.hpp"
#include "logger.hpp"

namespace lite {

template <typename Application, typename Packet, typename Connection,
          typename CacheKey, typename CacheEntry, typename LogEntry>
  requires IsApplication<Application, Packet, Connection, CacheKey, CacheEntry,
                         LogEntry>
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

  void Serve(Packet p, Connection &conn) {
    if (emergency_mode_) {
      app_.EmergencyServe(std::move(p), conn, cache_, logger_);
    } else {
      if (app_.Filter(p, conn)) {
        app_.NormalUpdate(p, conn, cache_);
      }

      // Forward
      if (conn.backend_fd_ <= 0) {
        if (!conn.ConnectBackend()) {
          std::cerr << "Fall back to EmergencyServe\n";
          emergency_mode_ = true;
          app_.EmergencyServe(std::move(p), conn, cache_, logger_);
          return;
        }
      }
      // TODO: do we really need to have this copy?
      std::vector<uint8_t> buffer;
      p->AppendToBuffer(buffer);

      write(conn.backend_fd_, buffer.data(), buffer.size());
    }
  }

 private:
  std::string &backend_addr_, &backend_port_;

  Application &app_;

  Cache<CacheKey, CacheEntry> cache_;

  Logger<LogEntry> logger_;

  void Replay() {
    int backend_fd, tries = 0;
    while ((backend_fd = Connection::TryConnectBackend(backend_addr_,
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
      const auto buffer = entry.Serialize();
      write(backend_fd, buffer.data(), buffer.size());  // TODO: less writes
    }
    // TODO: in-flight requests after this?
    close(backend_fd);
    std::cerr << "Replay " << cnt << " items\n";
  }
};

}  // namespace lite
