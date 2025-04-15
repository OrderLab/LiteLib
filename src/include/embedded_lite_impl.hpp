#pragma once

#include <sys/un.h>
#include <ctime>

#include "cache.hpp"
#include "embedded_lite.h"
#include "embedded_server.hpp"

namespace lite {

extern void *embedded_server_void_ptr;

template <typename Application, typename Request, typename Response,
          typename ConnectionInfo, typename CacheKey, typename CacheEntry>
  requires IsApplication<Application, Request, Response, ConnectionInfo,
                         CacheKey, CacheEntry> &&
           IsProtocolMessage<Request> && IsProtocolMessage<Response> &&
           IsCacheEntry<Request, CacheKey, CacheEntry>
int IsNormalMode() {
  if (unlikely(!embedded_server_void_ptr))  // called after SignalHandler
    return -1;
  auto embedded_server_ptr =
      static_cast<EmbeddedServer<Application, Request, Response, ConnectionInfo,
                                 CacheKey, CacheEntry> *>(
          embedded_server_void_ptr);
  return !embedded_server_ptr->emergency_mode_;
}

template <typename Application, typename Request, typename Response,
          typename ConnectionInfo, typename CacheKey, typename CacheEntry>
  requires IsApplication<Application, Request, Response, ConnectionInfo,
                         CacheKey, CacheEntry> &&
           IsProtocolMessage<Request> && IsProtocolMessage<Response> &&
           IsCacheEntry<Request, CacheKey, CacheEntry>
int Init(char *argv_0, const std::string socket_path,
         FlushWriteBufferFn FlushWriteBuffer,
         ReinstallClientEventHandlerFn ReinstallClientEventHandler,
         ReinstallListenerEventHandlerFn ReinstallListenerEventHandler) {
  google::InitGoogleLogging(argv_0);

  auto embedded_server_ptr =
      new EmbeddedServer<Application, Request, Response, ConnectionInfo,
                         CacheKey, CacheEntry>(socket_path, FlushWriteBuffer,
                                               ReinstallClientEventHandler,
                                               ReinstallListenerEventHandler);
  embedded_server_void_ptr = embedded_server_ptr;
  LOG(INFO) << "eBPF LiteSys initialized";
  LOG(INFO) << "\temergency_mode: " << embedded_server_ptr->emergency_mode_;

  srand(static_cast<unsigned int>(time(nullptr)));

  return 0;
}

inline int ConnectToLiteProcess(const std::string &socket_path) {
  struct sockaddr_un addr;
  int fd = socket(AF_UNIX, SOCK_STREAM, 0);
  if (fd < 0) {
    LOG(ERROR) << "Failed to create unix domain socket";
    return -2;
  }

  memset(&addr, 0, sizeof(addr));
  addr.sun_family = AF_UNIX;
  strncpy(addr.sun_path, socket_path.c_str(), sizeof(addr.sun_path) - 1);

  if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
    LOG(ERROR) << "Failed to connect to " << socket_path;
    close(fd);
    return -2;
  }

  return fd;
}

template <typename Application, typename Request, typename Response,
          typename ConnectionInfo, typename CacheKey, typename CacheEntry>
  requires IsApplication<Application, Request, Response, ConnectionInfo,
                         CacheKey, CacheEntry> &&
           IsProtocolMessage<Request> && IsProtocolMessage<Response> &&
           IsCacheEntry<Request, CacheKey, CacheEntry>
int FullStartListening() {
  auto embedded_server_ptr =
      static_cast<EmbeddedServer<Application, Request, Response, ConnectionInfo,
                                 CacheKey, CacheEntry> *>(
          embedded_server_void_ptr);
  if (!embedded_server_ptr->emergency_mode_) return 0;

  // transition from emergency mode to normal mode
  auto lite_fd = ConnectToLiteProcess(embedded_server_ptr->socket_path_);
  if (lite_fd < 0) return lite_fd;

  embedded_server_ptr->TransitionToNormalMode(lite_fd);
  return 0;
}

template <typename Application, typename Request, typename Response,
          typename ConnectionInfo, typename CacheKey, typename CacheEntry>
  requires IsApplication<Application, Request, Response, ConnectionInfo,
                         CacheKey, CacheEntry> &&
           IsProtocolMessage<Request> && IsProtocolMessage<Response> &&
           IsCacheEntry<Request, CacheKey, CacheEntry>
int SignalHandler(int sig) {
  auto embedded_server_ptr =
      static_cast<EmbeddedServer<Application, Request, Response, ConnectionInfo,
                                 CacheKey, CacheEntry> *>(
          embedded_server_void_ptr);
  auto &FlushWriteBuffer = embedded_server_ptr->FlushWriteBuffer;
  // TODO: do we need to stop other threads?
  // TODO: do we need to transfer application read buffer?

  std::vector<int> fds;
  auto dummy_fd = open("/dev/null", O_RDWR);
  if (dummy_fd == -1) {
    LOG(ERROR) << "Failed to create a dummy socket";
    return -1;
  }

  for (auto &[fd, arg] : embedded_server_ptr->fd_to_arg_) {
    FlushWriteBuffer(arg);

    int new_fd = network::CopyAndReplaceSocket(
        fd, dummy_fd);  // prevent the old one from being closed
    if (new_fd < 0) {
      LOG(ERROR) << "Failed to transfer client socket " << fd;
      continue;
    }

    fds.push_back(new_fd);
  }

  std::array<int, 2> lens = {static_cast<int>(fds.size()), 0};

  for (auto listener_fd : embedded_server_ptr->listener_fds_) {
    int new_fd = network::CopyAndReplaceSocket(
        listener_fd, dummy_fd);  // prevent the old one from being closed
    if (new_fd < 0) {
      LOG(ERROR) << "Failed to transfer listener socket " << listener_fd;
      continue;
    }

    fds.push_back(new_fd);
  }

  lens[1] = fds.size();

  auto lite_fd = ConnectToLiteProcess(embedded_server_ptr->socket_path_);
  if (lite_fd < 0) return lite_fd;

  if (!network::SendSockets(lite_fd, fds, lens)) {
    LOG(ERROR) << "Failed to transfer sockets to the lite process";
    close(lite_fd);
    return -1;
  }

  // clean up
  close(dummy_fd);
  for (auto fd : fds) {
    close(fd);
  }

  delete embedded_server_ptr;
  embedded_server_void_ptr = nullptr;
  return 0;
}

template <typename Application, typename Request, typename Response,
          typename ConnectionInfo, typename CacheKey, typename CacheEntry>
  requires IsApplication<Application, Request, Response, ConnectionInfo,
                         CacheKey, CacheEntry> &&
           IsProtocolMessage<Request> && IsProtocolMessage<Response> &&
           IsCacheEntry<Request, CacheKey, CacheEntry>
void RegisterListener(int fd, void *listener, int is_replay) {
  if (unlikely(!embedded_server_void_ptr))  // called after SignalHandler
    return;
  if (is_replay) return;
  auto embedded_server_ptr =
      static_cast<EmbeddedServer<Application, Request, Response, ConnectionInfo,
                                 CacheKey, CacheEntry> *>(
          embedded_server_void_ptr);
  embedded_server_ptr->listener_fds_.insert(fd);
  if (embedded_server_ptr->emergency_mode_)
    embedded_server_ptr->fd_to_listener_[fd] = listener;
}

template <typename Application, typename Request, typename Response,
          typename ConnectionInfo, typename CacheKey, typename CacheEntry>
  requires IsApplication<Application, Request, Response, ConnectionInfo,
                         CacheKey, CacheEntry> &&
           IsProtocolMessage<Request> && IsProtocolMessage<Response> &&
           IsCacheEntry<Request, CacheKey, CacheEntry>
void UnregisterListener(int fd) {
  if (unlikely(!embedded_server_void_ptr))  // called after SignalHandler
    return;
  auto embedded_server_ptr =
      static_cast<EmbeddedServer<Application, Request, Response, ConnectionInfo,
                                 CacheKey, CacheEntry> *>(
          embedded_server_void_ptr);
  embedded_server_ptr->listener_fds_.erase(fd);
  if (embedded_server_ptr->emergency_mode_)
    embedded_server_ptr->fd_to_listener_.erase(fd);
}

template <typename Application, typename Request, typename Response,
          typename ConnectionInfo, typename CacheKey, typename CacheEntry>
  requires IsApplication<Application, Request, Response, ConnectionInfo,
                         CacheKey, CacheEntry> &&
           IsProtocolMessage<Request> && IsProtocolMessage<Response> &&
           IsCacheEntry<Request, CacheKey, CacheEntry>
int GetDummyListenerFD() {
  if (unlikely(!embedded_server_void_ptr))  // called after SignalHandler
    return -1;
  auto embedded_server_ptr =
      static_cast<EmbeddedServer<Application, Request, Response, ConnectionInfo,
                                 CacheKey, CacheEntry> *>(
          embedded_server_void_ptr);
  if (!embedded_server_ptr->emergency_mode_) {
    LOG(ERROR) << "GetDummyListenerFD called in normal mode";
    return -1;
  }

  // Generate random string for socket path
  std::string random_str;
  const char charset[] = "0123456789abcdefghijklmnopqrstuvwxyz";
  random_str.reserve(16);
  for (int i = 0; i < 16; i++) {
    random_str += charset[rand() % sizeof(charset)];
  }
  std::string socket_path = "/tmp/ebpf.dummy." + random_str + ".sock";

  // Create random unix domain socket
  int fd = socket(AF_UNIX, SOCK_STREAM, 0);
  if (fd == -1) {
    LOG(ERROR) << "Failed to create unix socket";
    return -1;
  }

  struct sockaddr_un addr;
  memset(&addr, 0, sizeof(addr));
  addr.sun_family = AF_UNIX;
  strncpy(addr.sun_path, socket_path.c_str(), sizeof(addr.sun_path) - 1);

  unlink(socket_path.c_str());
  if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) == -1) {
    LOG(ERROR) << "Failed to bind unix socket";
    close(fd);
    return -1;
  }

  if (listen(fd, SOMAXCONN) == -1) {
    LOG(ERROR) << "Failed to listen on unix socket";
    close(fd);
    return -1;
  }

  LOG(INFO) << "GetDummyListenerFD: " << fd << " at " << socket_path;
  return fd;
}

template <typename Application, typename Request, typename Response,
          typename ConnectionInfo, typename CacheKey, typename CacheEntry>
  requires IsApplication<Application, Request, Response, ConnectionInfo,
                         CacheKey, CacheEntry> &&
           IsProtocolMessage<Request> && IsProtocolMessage<Response> &&
           IsCacheEntry<Request, CacheKey, CacheEntry>
void RegisterClient(int fd, void *client) {
  if (unlikely(!embedded_server_void_ptr))  // called after SignalHandler
    return;
  auto embedded_server_ptr =
      static_cast<EmbeddedServer<Application, Request, Response, ConnectionInfo,
                                 CacheKey, CacheEntry> *>(
          embedded_server_void_ptr);
  if (!embedded_server_ptr->emergency_mode_) {
    embedded_server_ptr->fd_to_arg_[fd] = client;
    return;
  } else {
    embedded_server_ptr->fd_to_arg_[fd] = client;
    embedded_server_ptr->replay_conns_.push(std::make_pair(fd, client));
    return;
  }
  unreachable;
}

template <typename Application, typename Request, typename Response,
          typename ConnectionInfo, typename CacheKey, typename CacheEntry>
  requires IsApplication<Application, Request, Response, ConnectionInfo,
                         CacheKey, CacheEntry> &&
           IsProtocolMessage<Request> && IsProtocolMessage<Response> &&
           IsCacheEntry<Request, CacheKey, CacheEntry>
void UnregisterClient(int fd) {
  if (unlikely(!embedded_server_void_ptr))  // called after SignalHandler
    return;
  auto embedded_server_ptr =
      static_cast<EmbeddedServer<Application, Request, Response, ConnectionInfo,
                                 CacheKey, CacheEntry> *>(
          embedded_server_void_ptr);
  if (!embedded_server_ptr->emergency_mode_) {
    auto arg = embedded_server_ptr->fd_to_arg_[fd];
    embedded_server_ptr->fd_to_arg_.erase(fd);
  } else {
    LOG(WARNING) << "Replay connection disconnected in emergency mode";
  }
}

}  // namespace lite
