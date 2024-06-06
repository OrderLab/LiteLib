#pragma once

#include <event.h>
#include <pthread.h>

#include <atomic>
#include <functional>
#include <string>

namespace lite {

class Daemon {
 public:
  explicit Daemon(const std::function<bool()> &Replay,
                  std::function<void()> TakeOver, std::string &backend_port,
                  const std::string pipe_path = "/tmp/lite");

  std::atomic<bool> emergency_mode_ = false;

  static size_t GetUNIXTimeStamp();

 private:
  std::function<bool()> Crash_;

  std::function<bool()> Replay_;

  std::function<void()> TakeOver_;

  pthread_t thread_id_;

  int named_pipe_fd_;

  struct event_base *base_;

  struct event pipe_event_;

  std::string pipe_path_;

  std::string &backend_port_;

  void CreatePipeAndRegisterEvent();

  static void *ThreadBody(void *arg);

  static void PipeHandler(evutil_socket_t fd, short which, void *arg_self);
};

}  // namespace lite