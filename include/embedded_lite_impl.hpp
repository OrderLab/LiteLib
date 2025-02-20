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
int Init(char *argv_0, int number_of_workers, long long shared_memory_size,
         long long max_item_count,
         const std::chrono::milliseconds sliding_window_size,
         const std::string socket_path, RequestDestructorFn RequestDestructor,
         FlushWriteBufferFn FlushWriteBuffer,
         ReinstallEventHandlerFn ReinstallEventHandler) {
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
          ReinstallEventHandler);
  LOG(INFO) << "Embedded LiteSys initialized";
  LOG(INFO) << "\tnumber_of_workers: " << number_of_workers;
  LOG(INFO) << "\tmax_item_count: " << max_item_count;
  LOG(INFO) << "\tsliding_window_size_in_ms: " << sliding_window_size.count();

  return 0;
}

// replace the socket under fd with a new one to prevent the old one from
// being closed
#define CopyAndReplaceSocket(fd, dummy_fd)                \
  ({                                                      \
    int new_fd = dup(fd);                                 \
    if (new_fd == -1) {                                   \
      PLOG(ERROR) << "Failed to duplicate socket " << fd; \
      new_fd = -1;                                        \
    } else {                                              \
      if (dup2(dummy_fd, fd) != fd) {                     \
        PLOG(ERROR) << "Failed to hijack socket " << fd;  \
        close(new_fd);                                    \
        new_fd = -1;                                      \
      }                                                   \
    }                                                     \
    new_fd;                                               \
  })

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

    int new_fd = CopyAndReplaceSocket(fd, dummy_fd);
    if (new_fd < 0) {
      LOG(ERROR) << "Failed to transfer client socket " << fd;
      continue;
    }

    fds.push_back(new_fd);
  }

  std::array<int, 2> lens = {static_cast<int>(fds.size()), 0};

  for (auto listener_fd : embedded_server_ptr->listener_fds_) {
    int new_fd = CopyAndReplaceSocket(listener_fd, dummy_fd);
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

  struct sockaddr_un addr;
  int sock = socket(AF_UNIX, SOCK_STREAM, 0);
  if (sock < 0) {
    LOG(ERROR) << "Failed to create unix domain socket";
    return -2;
  }

  memset(&addr, 0, sizeof(addr));
  addr.sun_family = AF_UNIX;
  strncpy(addr.sun_path, embedded_server_ptr->socket_path_.c_str(),
          sizeof(addr.sun_path) - 1);

  if (connect(sock, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
    LOG(ERROR) << "Failed to connect to " << embedded_server_ptr->socket_path_;
    close(sock);
    return -2;
  }

  if (sendmsg(sock, &msg, 0) < 0) {
    LOG(ERROR) << "Failed to transfer sockets to the lite process";
  }

  delete embedded_server_ptr;
  return 0;
}

#undef CopyAndReplaceSocket

template <typename Application, typename Request, typename Response,
          typename ConnectionInfo, typename CacheKey, typename CacheEntry>
  requires IsApplication<Application, Request, Response, ConnectionInfo,
                         CacheKey, CacheEntry> &&
           IsProtocolMessage<Request> && IsProtocolMessage<Response> &&
           IsConnectionInfo<ConnectionInfo> && IsCacheKey<CacheKey> &&
           IsCacheEntry<Request, CacheKey, CacheEntry>
void RegisterListenerFD(int fd) {
  auto embedded_server_ptr =
      static_cast<EmbeddedServer<Application, Request, Response, ConnectionInfo,
                                 CacheKey, CacheEntry> *>(
          embedded_server_void_ptr);
  embedded_server_ptr->listener_fds_.insert(fd);
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
  auto tcp_id = network::GetTCPID(fd);
  embedded_server_ptr->fd_to_tcp_id_and_arg_[fd] = {tcp_id, client};

  auto connection_state_ptr =
      embedded_server_ptr->connection_state_storage_ptr_->Get(tcp_id);
  if (connection_state_ptr) {
    LOG(WARNING) << "Connection already registered, deleting old one";
    embedded_server_ptr->connection_state_storage_ptr_->Delete(tcp_id);
  }
  connection_state_ptr =
      embedded_server_ptr->connection_state_storage_ptr_->Add(tcp_id);

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
  auto tcp_id_and_arg = embedded_server_ptr->fd_to_tcp_id_and_arg_[fd];
  if (!embedded_server_ptr->connection_state_storage_ptr_->Delete(
          tcp_id_and_arg.first)) {
    LOG(WARNING) << "Connection not registered";
  }
  embedded_server_ptr->fd_to_tcp_id_and_arg_.erase(fd);
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
  auto job =
      new EmbeddedNormalUpdateMessage<Application, Request, Response,
                                      ConnectionInfo, CacheKey, CacheEntry>{
          conn_info, request, std::move(NormalUpdate)};

  EmbeddedWorkerMessage msg;
  msg.type = EmbeddedWorkerMessage::Type::kNormalUpdate;
  msg.data = job;

  auto embedded_server_ptr =
      static_cast<EmbeddedServer<Application, Request, Response, ConnectionInfo,
                                 CacheKey, CacheEntry> *>(
          embedded_server_void_ptr);
  embedded_server_ptr->SendMessageToNextWorker(msg);
  return 0;
}

}  // namespace lite
