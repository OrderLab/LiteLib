#pragma once

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
           IsConnectionInfo<ConnectionInfo> && IsCacheKey<CacheKey> &&
           IsCacheEntry<Request, CacheKey, CacheEntry>
int IsNormalMode() {
  auto embedded_server_ptr =
      static_cast<EmbeddedServer<Application, Request, Response, ConnectionInfo,
                                 CacheKey, CacheEntry> *>(
          embedded_server_void_ptr);
  return !embedded_server_ptr->emergency_mode_ptr_->load();
}

template <typename Application, typename Request, typename Response,
          typename ConnectionInfo, typename CacheKey, typename CacheEntry>
  requires IsApplication<Application, Request, Response, ConnectionInfo,
                         CacheKey, CacheEntry> &&
           IsProtocolMessage<Request> && IsProtocolMessage<Response> &&
           IsConnectionInfo<ConnectionInfo> && IsCacheKey<CacheKey> &&
           IsCacheEntry<Request, CacheKey, CacheEntry>
int Init(char *argv_0, int number_of_workers, long long shared_memory_size,
         long long max_item_count,
         const std::chrono::milliseconds sliding_window_size,
         const std::string socket_path, RequestDestructorFn RequestDestructor,
         FlushWriteBufferFn FlushWriteBuffer,
         ReinstallClientEventHandlerFn ReinstallClientEventHandler,
         ReinstallListenerEventHandlerFn ReinstallListenerEventHandler) {
  google::InitGoogleLogging(argv_0);
  std::cerr << "\033[31mEmbedded LiteSys [INFO] messages are printed to "
               "/tmp/${full-version}.*\033[0m"
            << std::endl;

  if (number_of_workers != 1) {
    LOG(ERROR) << "LiteSys only supports 1 worker in embedded mode now";
    return 1;
  }

  embedded_server_void_ptr =
      new EmbeddedServer<Application, Request, Response, ConnectionInfo,
                         CacheKey, CacheEntry>(
          number_of_workers, shared_memory_size, max_item_count,
          sliding_window_size, socket_path, RequestDestructor, FlushWriteBuffer,
          ReinstallClientEventHandler, ReinstallListenerEventHandler);
  LOG(INFO) << "Embedded LiteSys initialized";
  LOG(INFO) << "\tnumber_of_workers: " << number_of_workers;
  LOG(INFO) << "\tmax_item_count: " << max_item_count;
  LOG(INFO) << "\tsliding_window_size_in_ms: " << sliding_window_size.count();

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

// replace the socket under fd with a new one
inline int CopyAndReplaceSocket(int fd, int dummy_fd) {
  int new_fd = dup(fd);
  if (new_fd == -1) {
    PLOG(ERROR) << "Failed to duplicate socket " << fd;
    return -1;
  }
  if (dup2(dummy_fd, fd) != fd) {
    PLOG(ERROR) << "Failed to hijack socket " << fd;
    close(new_fd);
    return -1;
  }
  return new_fd;
}

template <typename Application, typename Request, typename Response,
          typename ConnectionInfo, typename CacheKey, typename CacheEntry>
  requires IsApplication<Application, Request, Response, ConnectionInfo,
                         CacheKey, CacheEntry> &&
           IsProtocolMessage<Request> && IsProtocolMessage<Response> &&
           IsConnectionInfo<ConnectionInfo> && IsCacheKey<CacheKey> &&
           IsCacheEntry<Request, CacheKey, CacheEntry>
int FullStartListening() {
  static constexpr size_t kMaxFds = 1024;

  auto embedded_server_ptr =
      static_cast<EmbeddedServer<Application, Request, Response, ConnectionInfo,
                                 CacheKey, CacheEntry> *>(
          embedded_server_void_ptr);
  if (!embedded_server_ptr->emergency_mode_ptr_->load()) return 0;

  // transition from emergency mode to normal mode
  auto lite_fd = ConnectToLiteProcess(embedded_server_ptr->socket_path_);
  if (lite_fd < 0) return lite_fd;

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

  ssize_t received = recvmsg(lite_fd, &msg, 0);
  if (received < 0) {
    LOG(ERROR) << "LiteSys: Error receiving socket message";
    close(lite_fd);
    return -1;
  }

  struct cmsghdr *cmsg = CMSG_FIRSTHDR(&msg);
  if (!cmsg || cmsg->cmsg_level != SOL_SOCKET ||
      cmsg->cmsg_type != SCM_RIGHTS) {
    LOG(ERROR) << "LiteSys: Invalid control message";
    close(lite_fd);
    return -1;
  }

  size_t num_fds = lens[1];
  if (num_fds > kMaxFds) {
    LOG(ERROR) << "LiteSys: Too many FDs received";
    close(lite_fd);
    return -1;
  }

  std::vector<int> received_fds(num_fds);
  memcpy(received_fds.data(), CMSG_DATA(cmsg), sizeof(int) * num_fds);
  LOG(INFO) << "LiteSys: Received " << lens[0] << " client FDs and "
            << (lens[1] - lens[0]) << " listener FDs";

  for (int i = 0; i < lens[0]; i++) {
    auto [replay_fd, client] = embedded_server_ptr->replay_conns_.front();
    embedded_server_ptr->replay_conns_.pop();
    auto original_fd = CopyAndReplaceSocket(replay_fd, received_fds[i]);
    if (original_fd < 0) {
      LOG(ERROR) << "Failed to hijack client socket: replay_fd=" << replay_fd
                 << ", new_fd=" << received_fds[i];
      continue;
    }
    close(replay_fd);
    embedded_server_ptr->ReinstallClientEventHandler(client);
  }

  if (embedded_server_ptr->fd_to_listener_.size() != lens[1] - lens[0]) {
    LOG(ERROR) << "LiteSys: Number of listener FDs mismatch";
    close(lite_fd);
    return -1;
  }
  int id = lens[0];
  for (auto &[fd, listener] : embedded_server_ptr->fd_to_listener_) {
    auto original_fd = CopyAndReplaceSocket(fd, received_fds[id++]);
    if (original_fd < 0) {
      LOG(ERROR) << "Failed to hijack listener socket: fd=" << fd
                 << ", new_fd=" << received_fds[id - 1];
      continue;
    }
    close(fd);
    embedded_server_ptr->ReinstallListenerEventHandler(listener);
  }

  embedded_server_ptr->emergency_mode_ptr_->store(false);

  return 0;
}

template <typename Application, typename Request, typename Response,
          typename ConnectionInfo, typename CacheKey, typename CacheEntry>
  requires IsApplication<Application, Request, Response, ConnectionInfo,
                         CacheKey, CacheEntry> &&
           IsProtocolMessage<Request> && IsProtocolMessage<Response> &&
           IsConnectionInfo<ConnectionInfo> && IsCacheKey<CacheKey> &&
           IsCacheEntry<Request, CacheKey, CacheEntry>
int SignalHandler(int sig) {
  auto embedded_server_ptr =
      static_cast<EmbeddedServer<Application, Request, Response, ConnectionInfo,
                                 CacheKey, CacheEntry> *>(
          embedded_server_void_ptr);
  auto &FlushWriteBuffer = embedded_server_ptr->FlushWriteBuffer;
  // TODO: do we need to stop other threads?
  // TODO: do we need to transfer application read buffer?

  embedded_server_ptr->notified_workers_count_.store(0);
  for (int i = 0; i < embedded_server_ptr->number_of_workers_; i++) {
    embedded_server_ptr->SendMessageToNextWorker(
        {EmbeddedWorkerMessage::Type::kSwitchToEmergencyMode, nullptr});
  }

  std::vector<int> fds;
  auto dummy_fd = open("/dev/null", O_RDWR);
  if (dummy_fd == -1) {
    LOG(ERROR) << "Failed to create a dummy socket";
    return -1;
  }

  for (auto &[fd, tcp_id_and_arg] :
       embedded_server_ptr->fd_to_tcp_id_and_arg_) {
    auto &[tcp_id, arg] = tcp_id_and_arg;
    FlushWriteBuffer(arg);

    int new_fd = CopyAndReplaceSocket(
        fd, dummy_fd);  // prevent the old one from being closed
    if (new_fd < 0) {
      LOG(ERROR) << "Failed to transfer client socket " << fd;
      continue;
    }

    fds.push_back(new_fd);
  }

  std::array<int, 2> lens = {static_cast<int>(fds.size()), 0};

  for (auto listener_fd : embedded_server_ptr->listener_fds_) {
    int new_fd = CopyAndReplaceSocket(
        listener_fd, dummy_fd);  // prevent the old one from being closed
    if (new_fd < 0) {
      LOG(ERROR) << "Failed to transfer listener socket " << listener_fd;
      continue;
    }

    fds.push_back(new_fd);
  }

  lens[1] = fds.size();

  while (embedded_server_ptr->notified_workers_count_.load() !=
         embedded_server_ptr->number_of_workers_) {
    std::this_thread::yield();
  }

  size_t totalBytes = sizeof(int) * fds.size();
  std::vector<char> cmsgBuf(CMSG_SPACE(totalBytes), 0);

  struct iovec iov;
  iov.iov_base = lens.data();
  iov.iov_len = sizeof(int) * lens.size();

  struct msghdr msg;
  memset(&msg, 0, sizeof(msg));
  msg.msg_iov = &iov;
  msg.msg_iovlen = 1;
  msg.msg_control = cmsgBuf.data();
  msg.msg_controllen = cmsgBuf.size();

  struct cmsghdr *cmsg = CMSG_FIRSTHDR(&msg);
  cmsg->cmsg_len = CMSG_LEN(totalBytes);
  cmsg->cmsg_level = SOL_SOCKET;
  cmsg->cmsg_type = SCM_RIGHTS;
  memcpy(CMSG_DATA(cmsg), fds.data(), totalBytes);

  auto lite_fd = ConnectToLiteProcess(embedded_server_ptr->socket_path_);
  if (lite_fd < 0) return lite_fd;

  if (sendmsg(lite_fd, &msg, 0) < 0) {
    LOG(ERROR) << "Failed to transfer sockets to the lite process";
  }

  delete embedded_server_ptr;
  return 0;
}

template <typename Application, typename Request, typename Response,
          typename ConnectionInfo, typename CacheKey, typename CacheEntry>
  requires IsApplication<Application, Request, Response, ConnectionInfo,
                         CacheKey, CacheEntry> &&
           IsProtocolMessage<Request> && IsProtocolMessage<Response> &&
           IsConnectionInfo<ConnectionInfo> && IsCacheKey<CacheKey> &&
           IsCacheEntry<Request, CacheKey, CacheEntry>
void RegisterListenerFD(int fd, void *listener) {
  auto embedded_server_ptr =
      static_cast<EmbeddedServer<Application, Request, Response, ConnectionInfo,
                                 CacheKey, CacheEntry> *>(
          embedded_server_void_ptr);
  embedded_server_ptr->listener_fds_.insert(fd);
  if (embedded_server_ptr->emergency_mode_ptr_->load())
    embedded_server_ptr->fd_to_listener_[fd] = listener;
}

template <typename Application, typename Request, typename Response,
          typename ConnectionInfo, typename CacheKey, typename CacheEntry>
  requires IsApplication<Application, Request, Response, ConnectionInfo,
                         CacheKey, CacheEntry> &&
           IsProtocolMessage<Request> && IsProtocolMessage<Response> &&
           IsConnectionInfo<ConnectionInfo> && IsCacheKey<CacheKey> &&
           IsCacheEntry<Request, CacheKey, CacheEntry>
void UnregisterListenerFD(int fd) {
  auto embedded_server_ptr =
      static_cast<EmbeddedServer<Application, Request, Response, ConnectionInfo,
                                 CacheKey, CacheEntry> *>(
          embedded_server_void_ptr);
  embedded_server_ptr->listener_fds_.erase(fd);
  if (embedded_server_ptr->emergency_mode_ptr_->load())
    embedded_server_ptr->fd_to_listener_.erase(fd);
}

template <typename Application, typename Request, typename Response,
          typename ConnectionInfo, typename CacheKey, typename CacheEntry>
  requires IsApplication<Application, Request, Response, ConnectionInfo,
                         CacheKey, CacheEntry> &&
           IsProtocolMessage<Request> && IsProtocolMessage<Response> &&
           IsConnectionInfo<ConnectionInfo> && IsCacheKey<CacheKey> &&
           IsCacheEntry<Request, CacheKey, CacheEntry>
int GetDummyListenerFD() {
  auto embedded_server_ptr =
      static_cast<EmbeddedServer<Application, Request, Response, ConnectionInfo,
                                 CacheKey, CacheEntry> *>(
          embedded_server_void_ptr);
  if (!embedded_server_ptr->emergency_mode_ptr_->load()) {
    LOG(ERROR) << "GetDummyListenerFD called in normal mode";
    return -1;
  }
  int fd = open("/dev/null", O_RDWR);
  if (fd == -1) {
    LOG(ERROR) << "Failed to create a dummy socket";
    return -1;
  }
  return fd;
}

template <typename Application, typename Request, typename Response,
          typename ConnectionInfo, typename CacheKey, typename CacheEntry>
  requires IsApplication<Application, Request, Response, ConnectionInfo,
                         CacheKey, CacheEntry> &&
           IsProtocolMessage<Request> && IsProtocolMessage<Response> &&
           IsConnectionInfo<ConnectionInfo> && IsCacheKey<CacheKey> &&
           IsCacheEntry<Request, CacheKey, CacheEntry>
void *RegisterClientFD(int fd, void *client) {
  auto embedded_server_ptr =
      static_cast<EmbeddedServer<Application, Request, Response, ConnectionInfo,
                                 CacheKey, CacheEntry> *>(
          embedded_server_void_ptr);
  void *connection_state_ptr = nullptr;
  if (!embedded_server_ptr->emergency_mode_ptr_->load()) {
    auto tcp_id = network::GetTCPID(fd);
    embedded_server_ptr->fd_to_tcp_id_and_arg_[fd] = {tcp_id, client};

    connection_state_ptr =
        embedded_server_ptr->connection_state_storage_ptr_->Get(tcp_id);
    if (connection_state_ptr) {
      LOG(WARNING) << "Connection already registered, deleting old one";
      embedded_server_ptr->connection_state_storage_ptr_->Delete(tcp_id);
    }
    connection_state_ptr =
        embedded_server_ptr->connection_state_storage_ptr_->Add(tcp_id);
  } else {
    auto tcp_id = embedded_server_ptr->connection_state_storage_ptr_
                      ->replay_conns_.pop_front();
    connection_state_ptr =
        embedded_server_ptr->connection_state_storage_ptr_->Get(tcp_id);
    if (!connection_state_ptr) {
      LOG(ERROR) << "Replay connection not registered";
      return nullptr;
    }
    embedded_server_ptr->replay_conns_.push(std::make_pair(fd, client));
  }
  return connection_state_ptr;
}

template <typename Application, typename Request, typename Response,
          typename ConnectionInfo, typename CacheKey, typename CacheEntry>
  requires IsApplication<Application, Request, Response, ConnectionInfo,
                         CacheKey, CacheEntry> &&
           IsProtocolMessage<Request> && IsProtocolMessage<Response> &&
           IsConnectionInfo<ConnectionInfo> && IsCacheKey<CacheKey> &&
           IsCacheEntry<Request, CacheKey, CacheEntry>
void UnregisterClientFD(int fd) {
  auto embedded_server_ptr =
      static_cast<EmbeddedServer<Application, Request, Response, ConnectionInfo,
                                 CacheKey, CacheEntry> *>(
          embedded_server_void_ptr);
  if (!embedded_server_ptr->emergency_mode_ptr_->load()) {
    auto tcp_id_and_arg = embedded_server_ptr->fd_to_tcp_id_and_arg_[fd];
    if (!embedded_server_ptr->connection_state_storage_ptr_->Delete(
            tcp_id_and_arg.first)) {
      LOG(WARNING) << "Connection not registered";
    }
    embedded_server_ptr->fd_to_tcp_id_and_arg_.erase(fd);

    // clear the conn info until all the previous requests are processed
    EmbeddedWorkerMessage msg;
    msg.type = EmbeddedWorkerMessage::Type::kConnectionDisconnect;
    msg.data = new EmbeddedConnectionDisconnectMessage{tcp_id_and_arg.first};
    embedded_server_ptr->SendMessageToNextWorker(msg);
  } else {
    LOG(WARNING) << "Replay connection disconnected in emergency mode";
  }
}

template <typename Application, typename Request, typename Response,
          typename ConnectionInfo, typename CacheKey, typename CacheEntry>
  requires IsApplication<Application, Request, Response, ConnectionInfo,
                         CacheKey, CacheEntry> &&
           IsProtocolMessage<Request> && IsProtocolMessage<Response> &&
           IsConnectionInfo<ConnectionInfo> && IsCacheKey<CacheKey> &&
           IsCacheEntry<Request, CacheKey, CacheEntry>
int ProcessRequest(void *conn_info, void *request,
                   NormalUpdateFn<Application, Request, Response,
                                  ConnectionInfo, CacheKey, CacheEntry>
                       NormalUpdate) {
  auto embedded_server_ptr =
      static_cast<EmbeddedServer<Application, Request, Response, ConnectionInfo,
                                 CacheKey, CacheEntry> *>(
          embedded_server_void_ptr);
  if (!embedded_server_ptr->emergency_mode_ptr_->load()) {
    auto job =
        new EmbeddedNormalUpdateMessage<Application, Request, Response,
                                        ConnectionInfo, CacheKey, CacheEntry>{
            conn_info, request, std::move(NormalUpdate)};

    EmbeddedWorkerMessage msg;
    msg.type = EmbeddedWorkerMessage::Type::kNormalUpdate;
    msg.data = job;

    embedded_server_ptr->SendMessageToNextWorker(msg);
    return 0;
  } else {
    // TODO: process error during replay
    return 0;
  }
}

}  // namespace lite
