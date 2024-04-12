#pragma once

#include <concepts>

#include "cache.hpp"
#include "logger.hpp"
#include "daemon.hpp"

namespace lite {

template <typename Service, typename Packet, typename Connection, typename Backend>
concept IsService = requires(Service service, Packet p, Connection c, Backend b) {
  // Whether it's an operation that contains state info and thus needs to be
  // cached e.g. UPDATE -> true, READ -> false
  { service.Filter(p, c) } -> std::convertible_to<bool>;

  // Perform the cachable operation during normal time
  { service.NormalUpdate(p, c) };

  // Forward any operation to the backend, get the response and return it to the
  // client during normal time
  { service.NormalForwardAndProxyBack(std::move(p), c, b) };

  // Perform any operation during emergency time
  { service.EmergencyServe(std::move(p), c) };

  // Sync the state changes during emergency time to the recovered full version
  { service.Replay() };
};

template <typename S, typename Packet, typename Connection, typename Backend>
  requires IsService<S, Packet, Connection, Backend>
class LiteServer : public Daemon {
 public:
  LiteServer(S &service, std::string &backend_port, const char pipe_path[])
      : Daemon([&] { service_.Replay(); }, backend_port, pipe_path),
        service_(service) {}

  void Serve(Packet p, Connection &conn, Backend backend) {
    if (IsInEmergencyMode()) {
      service_.EmergencyServe(std::move(p), conn);
    } else {
      if (service_.Filter(p, conn)) {
        service_.NormalUpdate(p, conn);
      }
      service_.NormalForwardAndProxyBack(std::move(p), conn, backend);
    }
  }

 private:
  S &service_;
};

}  // namespace lite
