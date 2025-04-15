#pragma once

#include <sys/epoll.h>
#include <sys/eventfd.h>

#include <cstdlib>
#include <thread>
#include <vector>

#include "cache_inner.hpp"
#include "logger_inner.hpp"

namespace lite {

template <typename Application, typename Request, typename Response,
          typename ConnectionInfo, typename CacheKey, typename CacheEntry>
class EmbeddedServer {
 public:
  EmbeddedServer(const std::string socket_path,
                 FlushWriteBufferFn FlushWriteBuffer,
                 ReinstallClientEventHandlerFn ReinstallClientEventHandler,
                 ReinstallListenerEventHandlerFn ReinstallListenerEventHandler)
      : FlushWriteBuffer(FlushWriteBuffer),
        ReinstallClientEventHandler(ReinstallClientEventHandler),
        ReinstallListenerEventHandler(ReinstallListenerEventHandler),
        socket_path_(socket_path) {
    const char *emergency_mode_env = std::getenv("LiteEmergencyMode");
    emergency_mode_ =
        emergency_mode_env != nullptr && std::string(emergency_mode_env) == "1";
  }

  bool emergency_mode_;

  FlushWriteBufferFn FlushWriteBuffer;
  ReinstallClientEventHandlerFn ReinstallClientEventHandler;
  ReinstallListenerEventHandlerFn ReinstallListenerEventHandler;

  std::set<int> listener_fds_;
  std::map<int, void *> fd_to_arg_;

  std::string socket_path_;

  std::queue<std::pair<int, void *>> replay_conns_;
  std::map<int, void *> fd_to_listener_;

 public:
  void TransitionToNormalMode(const int lite_fd) {
    std::thread transition_thread([this, lite_fd]() {
      int epoll_fd = epoll_create1(0);
      if (epoll_fd < 0) {
        LOG(ERROR) << "LiteSys: Failed to create epoll fd";
        close(lite_fd);
        return;
      }

      struct epoll_event ev;
      ev.events = EPOLLIN;
      ev.data.fd = lite_fd;
      if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, lite_fd, &ev) < 0) {
        LOG(ERROR) << "LiteSys: Failed to add lite_fd to epoll";
        close(epoll_fd);
        close(lite_fd);
        return;
      }

      auto [received_fds, lens] = network::ReceiveSockets(lite_fd);
      if (received_fds.empty()) {
        LOG(ERROR) << "LiteSys: Error receiving socket message";
        close(epoll_fd);
        close(lite_fd);
        return;
      }
      close(lite_fd);
      LOG(WARNING) << "LiteSys: Received " << lens[0] << " client FDs and "
                   << (lens[1] - lens[0]) << " listener FDs";

      emergency_mode_ = false;

      for (int i = 0; i < lens[0]; i++) {
        auto [replay_fd, client] = replay_conns_.front();
        replay_conns_.pop();
        ReinstallClientEventHandler(client, 0);
        auto original_fd =
            network::CopyAndReplaceSocket(replay_fd, received_fds[i]);
        if (original_fd < 0) {
          LOG(ERROR) << "Failed to hijack client socket: replay_fd="
                     << replay_fd << ", new_fd=" << received_fds[i];
          continue;
        }
        close(original_fd);
        ReinstallClientEventHandler(client, 1);
        close(received_fds[i]);
      }

      if (fd_to_listener_.size() != lens[1] - lens[0]) {
        LOG(ERROR) << "LiteSys: Number of listener FDs mismatch";
        close(epoll_fd);
        return;
      }
      int id = lens[0];
      for (auto &[fd, listener] : fd_to_listener_) {
        ReinstallListenerEventHandler(listener, 0);
        auto original_fd =
            network::CopyAndReplaceSocket(fd, received_fds[id++]);
        if (original_fd < 0) {
          LOG(ERROR) << "Failed to hijack listener socket: fd=" << fd
                     << ", new_fd=" << received_fds[id - 1];
          continue;
        }
        close(original_fd);
        ReinstallListenerEventHandler(listener, 1);
        close(received_fds[id - 1]);
      }

      close(epoll_fd);

      // clear state replay connections
      while (!replay_conns_.empty()) {
        replay_conns_.pop();
      }
    });

    transition_thread.detach();
  }
};

}  // namespace lite