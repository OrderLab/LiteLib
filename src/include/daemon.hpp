#pragma once

#include <event.h>
#include <pthread.h>

#include <atomic>
#include <functional>
#include <string>

namespace lite {

class Daemon {
 public:
  explicit Daemon(const std::function<void()> &Replay,
                  const std::string pipe_path = "/tmp/lite");

  bool IsInEmergencyMode() { return emergency_mode_; }

 private:
  std::function<void()> Replay;

  pthread_t thread_id_;

  int named_pipe_fd_;

  std::atomic<bool> emergency_mode_ = false;

  struct event_base *base_;

  struct event pipe_event_;

  std::string pipe_path_;

  void CreatePipeAndRegisterEvent();

  static void *ThreadBody(void *arg);

  static void PipeHandler(evutil_socket_t fd, short which, void *arg_self);
};

}  // namespace lite