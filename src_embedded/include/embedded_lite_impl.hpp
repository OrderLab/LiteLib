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
  if (unlikely(!embedded_server_void_ptr))  // called after SignalHandler
    return -1;
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
         ReinstallListenerEventHandlerFn ReinstallListenerEventHandler,
         std::function<int(void *request, ConnectionInfo &,
                           Cache<Application, Request, Response, ConnectionInfo,
                                 CacheKey, CacheEntry> *,
                           RequestDestructorFn)>
             NormalUpdate) {
  google::InitGoogleLogging(argv_0);

  if (number_of_workers != 1) {
    LOG(ERROR) << "LiteSys only supports 1 worker in embedded mode now";
    return 1;
  }

  auto embedded_server_ptr =
      new EmbeddedServer<Application, Request, Response, ConnectionInfo,
                         CacheKey, CacheEntry>(
          number_of_workers, shared_memory_size, max_item_count,
          sliding_window_size, socket_path, RequestDestructor, FlushWriteBuffer,
          ReinstallClientEventHandler, ReinstallListenerEventHandler,
          NormalUpdate);
  embedded_server_void_ptr = embedded_server_ptr;
  LOG(INFO) << "Embedded LiteSys initialized";
  LOG(INFO) << "\tnumber_of_workers: " << number_of_workers;
  LOG(INFO) << "\tmax_item_count: " << max_item_count;
  LOG(INFO) << "\tsliding_window_size_in_ms: " << sliding_window_size.count();
  LOG(INFO) << "\temergency_mode: "
            << embedded_server_ptr->emergency_mode_ptr_->load();

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
           IsConnectionInfo<ConnectionInfo> && IsCacheKey<CacheKey> &&
           IsCacheEntry<Request, CacheKey, CacheEntry>
int FullStartListening() {
  auto embedded_server_ptr =
      static_cast<EmbeddedServer<Application, Request, Response, ConnectionInfo,
                                 CacheKey, CacheEntry> *>(
          embedded_server_void_ptr);
  if (!embedded_server_ptr->emergency_mode_ptr_->load()) return 0;

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
           IsConnectionInfo<ConnectionInfo> && IsCacheKey<CacheKey> &&
           IsCacheEntry<Request, CacheKey, CacheEntry>
int SignalHandler(int sig) {
  const auto now = std::chrono::system_clock::now();
  const auto timestamp = std::chrono::duration_cast<std::chrono::nanoseconds>(
                         now.time_since_epoch()).count();
  LOG(INFO) << "EmbeddedLite: SignalHandler: " << timestamp;
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

  while (embedded_server_ptr->notified_workers_count_.load() !=
         embedded_server_ptr->number_of_workers_) {
    std::this_thread::yield();
  }

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
           IsConnectionInfo<ConnectionInfo> && IsCacheKey<CacheKey> &&
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
void UnregisterListener(int fd) {
  if (unlikely(!embedded_server_void_ptr))  // called after SignalHandler
    return;
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
  if (unlikely(!embedded_server_void_ptr))  // called after SignalHandler
    return -1;
  auto embedded_server_ptr =
      static_cast<EmbeddedServer<Application, Request, Response, ConnectionInfo,
                                 CacheKey, CacheEntry> *>(
          embedded_server_void_ptr);
  if (!embedded_server_ptr->emergency_mode_ptr_->load()) {
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
  std::string socket_path = "/tmp/" + random_str + ".sock";

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
           IsConnectionInfo<ConnectionInfo> && IsCacheKey<CacheKey> &&
           IsCacheEntry<Request, CacheKey, CacheEntry>
void *RegisterClient(int fd, void *client) {
  if (unlikely(!embedded_server_void_ptr))  // called after SignalHandler
    return nullptr;
  auto embedded_server_ptr =
      static_cast<EmbeddedServer<Application, Request, Response, ConnectionInfo,
                                 CacheKey, CacheEntry> *>(
          embedded_server_void_ptr);
  if (!embedded_server_ptr->emergency_mode_ptr_->load()) {
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
  } else {
    auto tcp_id = embedded_server_ptr->connection_state_storage_ptr_
                      ->replay_conns_.pop_front();
    embedded_server_ptr->fd_to_tcp_id_and_arg_[fd] = {tcp_id, client};

    auto connection_state_ptr =
        embedded_server_ptr->connection_state_storage_ptr_->Get(tcp_id);
    if (!connection_state_ptr) {
      LOG(ERROR) << "Replay connection not registered";
      return nullptr;
    }
    embedded_server_ptr->replay_conns_.push(std::make_pair(fd, client));
    return connection_state_ptr;
  }
  unreachable;
}

template <typename Application, typename Request, typename Response,
          typename ConnectionInfo, typename CacheKey, typename CacheEntry>
  requires IsApplication<Application, Request, Response, ConnectionInfo,
                         CacheKey, CacheEntry> &&
           IsProtocolMessage<Request> && IsProtocolMessage<Response> &&
           IsConnectionInfo<ConnectionInfo> && IsCacheKey<CacheKey> &&
           IsCacheEntry<Request, CacheKey, CacheEntry>
void UnregisterClient(int fd) {
  if (unlikely(!embedded_server_void_ptr))  // called after SignalHandler
    return;
  auto embedded_server_ptr =
      static_cast<EmbeddedServer<Application, Request, Response, ConnectionInfo,
                                 CacheKey, CacheEntry> *>(
          embedded_server_void_ptr);
  if (!embedded_server_ptr->emergency_mode_ptr_->load()) {
    auto [tcp_id, arg] = embedded_server_ptr->fd_to_tcp_id_and_arg_[fd];
    embedded_server_ptr->fd_to_tcp_id_and_arg_.erase(fd);

    // clear the conn info until all the previous requests are processed
    EmbeddedWorkerMessage msg;
    msg.type = EmbeddedWorkerMessage::Type::kConnectionDisconnect;
    msg.data = new EmbeddedConnectionDisconnectMessage{tcp_id};
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
int ProcessRequest(void *conn_info, void *request, bool is_success) {
  if (unlikely(!embedded_server_void_ptr)) {
    LOG(ERROR) << "ProcessRequest called after SignalHandler";
    return -1;
  }
  auto embedded_server_ptr =
      static_cast<EmbeddedServer<Application, Request, Response, ConnectionInfo,
                                 CacheKey, CacheEntry> *>(
          embedded_server_void_ptr);
  if (likely(!embedded_server_ptr->emergency_mode_ptr_->load())) {
    if (unlikely(!is_success)) return 0;
    auto job =
        new EmbeddedNormalUpdateMessage<Application, Request, Response,
                                        ConnectionInfo, CacheKey, CacheEntry>{
            conn_info, request};

    EmbeddedWorkerMessage msg;
    msg.type = EmbeddedWorkerMessage::Type::kNormalUpdate;
    msg.data = job;

    embedded_server_ptr->SendMessageToNextWorker(msg);
    return 0;
  } else {
    if (likely(!is_success)) LOG(ERROR) << "Replay request failed";
    return 0;
  }
}

}  // namespace lite
