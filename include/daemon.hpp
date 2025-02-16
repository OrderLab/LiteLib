#pragma once

#include <event.h>
#include <pthread.h>

#include <atomic>
#include <functional>
#include <string>

#include "bip.hpp"

namespace lite {

class Daemon {
 public:
  explicit Daemon(const std::function<bool()> &Replay,
                  std::function<void()> TakeOver, std::string &backend_port,
                  const std::string pipe_path = "/tmp/lite");

  std::string &backend_port_;

  static size_t GetUNIXTimeStamp();

  void InitEmergencyModePtr(ShmAtomic<bool> *emergency_mode) {
    emergency_mode_ptr_ = emergency_mode;
  }

 private:
  ShmAtomic<bool> *emergency_mode_ptr_;

  std::function<bool()> Replay_;

  std::function<void()> TakeOver_;

  pthread_t thread_id_;

  int named_pipe_fd_;

  struct event_base *base_;

  struct event pipe_event_;

  std::string pipe_path_;

  void CreatePipeAndRegisterEvent();

  static void *ThreadBody(void *arg);

  static void PipeHandler(evutil_socket_t fd, short which, void *arg_self);
};

}  // namespace lite