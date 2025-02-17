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
         const std::chrono::milliseconds sliding_window_size) {
  google::InitGoogleLogging(argv_0);
  std::cerr << "\033[31mEmbedded LiteSys messages are printed to "
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
          sliding_window_size);
  LOG(INFO) << "Embedded LiteSys initialized";
  LOG(INFO) << "\tnumber_of_workers: " << number_of_workers;
  LOG(INFO) << "\tmax_item_count: " << max_item_count;
  LOG(INFO) << "\tsliding_window_size_in_ms: " << sliding_window_size.count();

  return 0;
}

template <typename Application, typename Request, typename Response,
          typename ConnectionInfo, typename CacheKey, typename CacheEntry>
  requires IsApplication<Application, Request, Response, ConnectionInfo,
                         CacheKey, CacheEntry> &&
           IsProtocolMessage<Request> && IsProtocolMessage<Response> &&
           IsConnectionInfo<ConnectionInfo> && IsCacheKey<CacheKey> &&
           IsCacheEntry<Request, CacheKey, CacheEntry>
int SignalHandler() {
  auto embedded_server_ptr =
      static_cast<EmbeddedServer<Application, Request, Response, ConnectionInfo,
                                 CacheKey, CacheEntry> *>(
          embedded_server_void_ptr);
  // TODO: how to stop other threads?
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
void *RegisterFD(int fd, ReinstallEventHandlerFn ReinstallEventHandler,
                 void *client) {
  auto embedded_server_ptr =
      static_cast<EmbeddedServer<Application, Request, Response, ConnectionInfo,
                                 CacheKey, CacheEntry> *>(
          embedded_server_void_ptr);
  auto tcp_id = network::GetTCPID(fd);
  embedded_server_ptr->fd_to_tcp_id_[fd] = tcp_id;

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
void UnregisterFD(int fd) {
  auto embedded_server_ptr =
      static_cast<EmbeddedServer<Application, Request, Response, ConnectionInfo,
                                 CacheKey, CacheEntry> *>(
          embedded_server_void_ptr);
  auto tcp_id = embedded_server_ptr->fd_to_tcp_id_[fd];
  if (!embedded_server_ptr->connection_state_storage_ptr_->Delete(tcp_id)) {
    LOG(WARNING) << "Connection not registered";
  }
  embedded_server_ptr->fd_to_tcp_id_.erase(fd);
}

template <typename Application, typename Request, typename Response,
          typename ConnectionInfo, typename CacheKey, typename CacheEntry>
  requires IsApplication<Application, Request, Response, ConnectionInfo,
                         CacheKey, CacheEntry> &&
           IsProtocolMessage<Request> && IsProtocolMessage<Response> &&
           IsConnectionInfo<ConnectionInfo> && IsCacheKey<CacheKey> &&
           IsCacheEntry<Request, CacheKey, CacheEntry>
int ProcessRequest(void *conn_info, void *request,
                   RequestDestructorFn RequestDestructor,
                   NormalUpdateFn<Application, Request, Response,
                                  ConnectionInfo, CacheKey, CacheEntry>
                       NormalUpdate) {
  auto job =
      new EmbeddedNormalUpdateMessage<Application, Request, Response,
                                      ConnectionInfo, CacheKey, CacheEntry>{
          conn_info, request, RequestDestructor, std::move(NormalUpdate)};

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
