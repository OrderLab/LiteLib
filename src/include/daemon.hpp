#pragma once

#include <event.h>
#include <pthread.h>
#include <sys/socket.h>
#include <sys/un.h>

#include <atomic>
#include <functional>
#include <string>

#include "bip.hpp"

namespace lite {

class Daemon {
 public:
  explicit Daemon(
      const std::function<bool()> &Replay,
      const std::function<void(const std::vector<int> &, int)> TakeOver,
      std::string &backend_port, const std::string socket_path = "/tmp/lite");

  std::string &backend_port_;

  static size_t GetUNIXTimeStamp();

  void InitEmergencyModePtr(ShmAtomic<bool> *emergency_mode) {
    emergency_mode_ptr_ = emergency_mode;
  }

 private:
  ShmAtomic<bool> *emergency_mode_ptr_;

  std::function<bool()> Replay_;

  std::function<void(const std::vector<int> &, int)> TakeOver_;

  pthread_t thread_id_;

  int socket_fd_;

  struct event_base *base_;

  struct event socket_event_;

  std::string socket_path_;

  void CreateSocketAndRegisterEvent();

  static void *ThreadBody(void *arg);

  static void SocketHandler(evutil_socket_t fd, short which, void *arg_self);
};

}  // namespace lite