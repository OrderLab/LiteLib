#pragma once

#include <concepts>

#include "cache.hpp"
#include "daemon.hpp"
#include "logger.hpp"

namespace lite {

template <typename Application, typename Packet, typename Connection,
          typename Backend>
concept IsApplication =
    requires(Application app, Packet p, Connection c, Backend b) {
      // Whether it's an operation that contains state info and thus needs to be
      // cached e.g. UPDATE -> true, READ -> false
      { app.Filter(p, c) } -> std::convertible_to<bool>;

      // Perform the cachable operation during normal time
      { app.NormalUpdate(p, c) };

      // Forward any operation to the backend, get the response and return it to
      // the client during normal time
      { app.NormalForwardAndProxyBack(std::move(p), c, b) };

      // Perform any operation during emergency time
      { app.EmergencyServe(std::move(p), c) };

      // Sync the state changes during emergency time to the recovered full
      // version
      { app.Replay() };
    };

template <typename Application, typename Packet, typename Connection,
          typename Backend>
  requires IsApplication<Application, Packet, Connection, Backend>
class LiteCore : public Daemon {
 public:
  LiteCore(Application &app, std::string &backend_port, const char pipe_path[])
      : Daemon([&] { app_.Replay(); }, backend_port, pipe_path), app_(app) {}

  void Serve(Packet p, Connection &conn, Backend backend) {
    if (IsInEmergencyMode()) {
      app_.EmergencyServe(std::move(p), conn);
    } else {
      if (app_.Filter(p, conn)) {
        app_.NormalUpdate(p, conn);
      }
      app_.NormalForwardAndProxyBack(std::move(p), conn, backend);
    }
  }

 private:
  Application &app_;
};

}  // namespace lite
