#include "daemon.hpp"

#include <fcntl.h>
#include <sys/stat.h>

#include <chrono>
#include <cstring>
#include <iostream>
#include <stdexcept>

#define GLOG_USE_GLOG_EXPORT
#include <glog/logging.h>

namespace lite {

Daemon::Daemon(
    const std::function<bool()> &Replay,
    const std::function<void(const std::vector<int> &, int)> TakeOver,
    std::string &backend_port, const std::string socket_path)
    : Replay_(Replay),
      socket_path_(socket_path),
      backend_port_(backend_port),
      TakeOver_(TakeOver) {
  // set up event_base
  struct event_config *ev_config;
  ev_config = event_config_new();
  event_config_set_flag(ev_config, EVENT_BASE_FLAG_NOLOCK);
  base_ = event_base_new_with_config(ev_config);
  event_config_free(ev_config);

  CreateSocketAndRegisterEvent();

  // pthread create
  pthread_attr_t attr;
  pthread_attr_init(&attr);
  PCHECK(!pthread_create(&thread_id_, &attr, ThreadBody, this))
      << "Can't create daemon thread";
  pthread_setname_np(thread_id_, "lite-daemon");
  pthread_attr_destroy(&attr);
}

void Daemon::CreateSocketAndRegisterEvent() {
  // Create and bind Unix domain socket
  socket_fd_ = socket(AF_UNIX, SOCK_STREAM, 0);
  if (socket_fd_ == -1) {
    throw std::runtime_error("failed to create socket");
  }

  struct sockaddr_un addr;
  memset(&addr, 0, sizeof(addr));
  addr.sun_family = AF_UNIX;
  strncpy(addr.sun_path, socket_path_.c_str(), sizeof(addr.sun_path) - 1);

  // Remove existing socket file if it exists
  unlink(socket_path_.c_str());

  if (bind(socket_fd_, (struct sockaddr *)&addr, sizeof(addr)) == -1) {
    close(socket_fd_);
    throw std::runtime_error("failed to bind socket");
  }

  if (listen(socket_fd_, 5) == -1) {
    close(socket_fd_);
    throw std::runtime_error("failed to listen on socket");
  }

  LOG(INFO) << "Daemon listening on " << socket_path_ << std::endl;

  event_set(&socket_event_, socket_fd_, EV_READ | EV_PERSIST, SocketHandler,
            this);
  event_base_set(base_, &socket_event_);
  LOG_IF(FATAL, event_add(&socket_event_, 0) == -1)
      << "Can't monitor libevent socket\n";
}

void *Daemon::ThreadBody(void *arg_self) {
  Daemon *self = static_cast<Daemon *>(arg_self);

  event_base_loop(self->base_, 0);
  event_base_free(self->base_);

  return NULL;
}

void Daemon::SocketHandler(evutil_socket_t fd, short which, void *arg_self) {
  static constexpr size_t kMaxFds = 1024;
  Daemon *self = static_cast<Daemon *>(arg_self);

  if (fd == self->socket_fd_) {
    struct sockaddr_un client_addr;
    socklen_t client_len = sizeof(client_addr);
    int client_fd = accept(fd, (struct sockaddr *)&client_addr, &client_len);

    if (client_fd == -1) {
      LOG(ERROR) << "Daemon: Error accepting connection";
      return;
    }

    if (!self->emergency_mode_ptr_->load()) {
      std::array<int, 2> lens;
      std::vector<char> cmsgBuf(CMSG_SPACE(sizeof(int) * kMaxFds));

      struct iovec iov;
      iov.iov_base = lens.data();
      iov.iov_len = sizeof(lens);

      struct msghdr msg;
      memset(&msg, 0, sizeof(msg));
      msg.msg_iov = &iov;
      msg.msg_iovlen = 1;
      msg.msg_control = cmsgBuf.data();
      msg.msg_controllen = cmsgBuf.size();

      ssize_t received = recvmsg(client_fd, &msg, 0);
      if (received < 0) {
        LOG(ERROR) << "Daemon: Error receiving socket message";
        close(client_fd);
        return;
      }

      struct cmsghdr *cmsg = CMSG_FIRSTHDR(&msg);
      if (!cmsg || cmsg->cmsg_level != SOL_SOCKET ||
          cmsg->cmsg_type != SCM_RIGHTS) {
        LOG(ERROR) << "Daemon: Invalid control message";
        close(client_fd);
        return;
      }

      size_t num_fds = lens[1];
      if (num_fds > kMaxFds) {
        LOG(ERROR) << "Daemon: Too many FDs received";
        close(client_fd);
        return;
      }

      std::vector<int> received_fds(num_fds);
      memcpy(received_fds.data(), CMSG_DATA(cmsg), sizeof(int) * num_fds);
      LOG(INFO) << "Daemon: Received " << lens[0] << " client FDs and "
                << (lens[1] - lens[0]) << " listener FDs";
      self->TakeOver_(received_fds, lens[0]);
    } else {
      self->Replay_();
    }
    close(client_fd);
  }
}

size_t Daemon::GetUNIXTimeStamp() {
  const auto now = std::chrono::system_clock::now();
  return std::chrono::duration_cast<std::chrono::nanoseconds>(
             now.time_since_epoch())
      .count();
}

}  // namespace lite
