#include "daemon.hpp"

#include <fcntl.h>
#include <sys/stat.h>

#include <chrono>
#include <cstring>
#include <iostream>
#include <stdexcept>

#define GLOG_USE_GLOG_EXPORT
#include <glog/logging.h>

#include "pipe_message_def.hpp"

namespace lite {

Daemon::Daemon(const std::function<bool()> &Replay,
               std::function<void()> TakeOver,
               std::string &backend_port, const std::string pipe_path)
    : Replay_(Replay),
      pipe_path_(pipe_path),
      backend_port_(backend_port),
      TakeOver_(TakeOver){
  // set up event_base
  struct event_config *ev_config;
  ev_config = event_config_new();
  event_config_set_flag(ev_config, EVENT_BASE_FLAG_NOLOCK);
  base_ = event_base_new_with_config(ev_config);
  event_config_free(ev_config);

  CreatePipeAndRegisterEvent();

  // pthread create
  pthread_attr_t attr;
  pthread_attr_init(&attr);
  PCHECK(!pthread_create(&thread_id_, &attr, ThreadBody, this))
      << "Can't create daemon thread";
  pthread_setname_np(thread_id_, "lite-daemon");
  pthread_attr_destroy(&attr);
}

void Daemon::CreatePipeAndRegisterEvent() {
  // create pipe
  mkfifo(pipe_path_.c_str(), 0666);
  named_pipe_fd_ = open(pipe_path_.c_str(), O_RDONLY | O_NONBLOCK, 0);
  if (named_pipe_fd_ == -1) {
    throw std::runtime_error("failed to open the named pipe");
  }
  LOG(INFO) << "Deamon listening on " << pipe_path_ << std::endl;

  event_set(&pipe_event_, named_pipe_fd_, EV_READ, PipeHandler, this);
  event_base_set(base_, &pipe_event_);
  LOG_IF(FATAL, event_add(&pipe_event_, 0) == -1)
      << "Can't monitor libevent notify pipe\n";
};

void *Daemon::ThreadBody(void *arg_self) {
  Daemon *self = static_cast<Daemon *>(arg_self);

  event_base_loop(self->base_, 0);
  event_base_free(self->base_);

  return NULL;
}

void Daemon::PipeHandler(evutil_socket_t fd, short which, void *arg_self) {
  Daemon *self = static_cast<Daemon *>(arg_self);

  if (fd == self->named_pipe_fd_) {
    pipe_message_t message;
    auto bytes_read = read(fd, &message, sizeof(message));
    if (!bytes_read) return;
    if (bytes_read != sizeof(message)) {
      LOG(ERROR) << "Daemon: Error reading pipe";
      return;
    }
    // Close Pipe
    close(self->named_pipe_fd_);
    unlink(self->pipe_path_.c_str());
    self->CreatePipeAndRegisterEvent();
    self->backend_port_ = std::to_string(message.backend_port);
    switch (message.action) {
      case PipeMessage::kEnterEmergencyMode:
        LOG(WARNING) << "Daemon: Entering emergency mode " << GetUNIXTimeStamp() << std::endl;
        if (!self->emergency_mode_) {
          self->TakeOver_();
        } else {
          LOG(WARNING) << "Daemon: Already in emergency mode" << std::endl;
        }
        break;
      case PipeMessage::kExitEmergencyMode:
        LOG(WARNING) << "Daemon: Exiting emergency mode " << GetUNIXTimeStamp() << std::endl;
        if (!self->Replay_()) {
          LOG(ERROR) << "Daemon: Failed to replay" << std::endl;
          break;
        }
        break;
    }
  } else {
  }
}

size_t Daemon::GetUNIXTimeStamp() {
  const auto now = std::chrono::system_clock::now();
  return std::chrono::duration_cast<std::chrono::nanoseconds>(
             now.time_since_epoch())
      .count();
}

}  // namespace lite
