#pragma once

#include <event.h>
#include <netinet/tcp.h>

#include "connection.hpp"

namespace lite {

template <typename Application, typename Request, typename Response,
          typename ConnectionInfo, typename CacheKey, typename CacheEntry>
Connection<Application, Request, Response, ConnectionInfo, CacheKey,
           CacheEntry>::Connection(const evutil_socket_t sfd,
                                   const int event_flags,
                                   struct event_base* base,
                                   EventHandler event_handler,
                                   void* lite_server,
                                   LiteCoreInstance& lite_core,
                                   bool is_client_connection,
                                   WorkerInstance* worker_ptr,
                                   ConnectionStateInstance*
                                       connection_state_entry_ptr)
    : base_(base),
      client_fd_(sfd),
      backend_fd_(-1),
      request_(ShmMakeShared(
          lite_core.shared_memory_.get_segment_manager()
              ->template construct<Request>(bip::anonymous_instance)(
                  lite_core.shared_memory_.get_segment_manager()),
          lite_core.shared_memory_)),
      response_(ShmMakeShared(
          lite_core.shared_memory_.get_segment_manager()
              ->template construct<Response>(bip::anonymous_instance)(
                  lite_core.shared_memory_.get_segment_manager()),
          lite_core.shared_memory_)),
      lite_server_(lite_server),
      lite_core_(lite_core),
      self_(std::make_shared<ConnectionInstance*>(this)),
      worker_ptr_(worker_ptr),
      connection_state_entry_ptr_(connection_state_entry_ptr) {
  if (sfd) {
    if (!connection_state_entry_ptr_ && is_client_connection) {
      auto tcp_id = network::GetTCPID(sfd);
      connection_state_entry_ptr_ =
          lite_core_.connection_state_storage_ptr_->GetOrAdd(tcp_id);
    }

    event_set(&client_event_, sfd, event_flags, event_handler,
              static_cast<void*>(this));
    event_base_set(base, &client_event_);
    if (event_add(&client_event_, 0) == -1) {
      PLOG(ERROR) << "client event_add";
      throw std::runtime_error("client event_add");
    }
  } else {
    memset(&client_event_, 0, sizeof(client_event_));
  }

  memset(&backend_event_, 0, sizeof(backend_event_));

  if (is_client_connection && lite_core_.emergency_mode_ptr_->load()) {
    std::optional<Response> greeting_msg =
        lite_core_.app_.EmergencyConnectionEstablishHook(
            connection_state_entry_ptr_->extra_app_info_);
    if (greeting_msg.has_value()) {
      const auto buffer = greeting_msg.value().Serialize();
      if (!network::Write(client_fd_, buffer)) {
        LOG(ERROR) << "Failed to write greeting message to client" << std::endl;
        delete this;
      }
    }
  }
}

template <typename Application, typename Request, typename Response,
          typename ConnectionInfo, typename CacheKey, typename CacheEntry>
Connection<Application, Request, Response, ConnectionInfo, CacheKey,
           CacheEntry>::~Connection() {
  if (worker_ptr_) {
    worker_ptr_->conns_.erase(this);
  }
  lite_core_.live_connections_.erase(this);
  *self_ = nullptr;

  if (backend_fd_ > 0) close(backend_fd_);
  if (client_fd_ > 0) close(client_fd_);
  if (client_event_.ev_base) event_del(&client_event_);
  if (backend_event_.ev_base) event_del(&backend_event_);

  if (connection_state_entry_ptr_) {
    lite_core_.dead_connection_log_heads_.push_back(
        connection_state_entry_ptr_->log_head_.get());
  }
  // LOG(INFO) << "connection closed client_fd: " << client_fd_
  //           << " backend_fd: " << backend_fd_ << std::endl;
}

template <typename Application, typename Request, typename Response,
          typename ConnectionInfo, typename CacheKey, typename CacheEntry>
void Connection<Application, Request, Response, ConnectionInfo, CacheKey,
                CacheEntry>::ClientHandler(evutil_socket_t fd, short which,
                                           void* arg_conn) {
  auto conn = static_cast<Connection*>(arg_conn);
  if (fd != conn->client_fd_) {
    LOG(ERROR) << "ClientHandler: fd mismatch. Expecting " << conn->client_fd_
               << " but got " << fd << std::endl;
    return;
  }
  // TODO: handle the case when the buffer is not large enough
  // TODO: above TODOs apply to BackendHandler as well
  ssize_t bytes_transferred;
  if ((bytes_transferred = read(fd, conn->buffer_, 131072)) <= 0) {
    if (bytes_transferred == 0)
      ;  // LOG(INFO) << "Client disconnected: " << fd << std::endl;
    else
      ;  //   PLOG(ERROR) << "read from client";
    delete conn;
    // TODO: how to properly handle the case when the client disconnects as
    // expected? (e.g. quit command in Memcached)
    return;
  }

  bool forwarded = false;
  if (!conn->lite_core_.emergency_mode_ptr_->load()) {
    forwarded = true;
    if (!network::Write(conn->backend_fd_, conn->buffer_, bytes_transferred)) {
      LOG(ERROR) << "Failed to write request to backend" << std::endl;
      delete conn;
      return;
    }
  }

  uint8_t* begin = conn->buffer_;
  uint8_t* end = begin + bytes_transferred;
  while (begin != end) {
    const auto result = conn->request_->Deserialize(begin, end);
    if (result == kGood) {
      if (conn->backend_fd_ <= 0 &&
          !conn->lite_core_.emergency_mode_ptr_->load()) {
        conn->ConnectBackend();
      }
      if (!conn->lite_core_.HandleRequest(
              boost::move(conn->request_),
              conn->connection_state_entry_ptr_->extra_app_info_,
              conn->client_fd_, conn->backend_fd_,
              &conn->connection_state_entry_ptr_->cache_,
              &conn->connection_state_entry_ptr_->logger_, forwarded)) {
        delete conn;
        return;
      }
      conn->request_ = ShmMakeShared(
          conn->lite_core_.shared_memory_.get_segment_manager()
              ->template construct<Request>(bip::anonymous_instance)(
                  conn->lite_core_.shared_memory_.get_segment_manager()),
          conn->lite_core_.shared_memory_);
    } else if (result == kIndeterminate) {
      continue;
    } else if (result == kBad) {
      LOG(ERROR) << "failed to parse request" << std::endl;
      return;
    }
  }
  return;
}

template <typename Application, typename Request, typename Response,
          typename ConnectionInfo, typename CacheKey, typename CacheEntry>
void Connection<Application, Request, Response, ConnectionInfo, CacheKey,
                CacheEntry>::BackendHandler(evutil_socket_t fd, short which,
                                            void* arg_conn) {
  auto conn = static_cast<Connection*>(arg_conn);
  if (fd != conn->backend_fd_) {
    // TODO: what if the client disconnects but the backend still sends a
    // response?
    LOG(ERROR) << "BackendHandler: fd mismatch. Expecting " << conn->backend_fd_
               << " but got " << fd << std::endl;
    return;
  }

  ssize_t bytes_transferred;
  if ((bytes_transferred = read(fd, conn->buffer_, 131072)) <= 0) {
    if (bytes_transferred == 0) {
      ;  // LOG(WARNING) << "Backend disconnected: " << fd << std::endl;
      close(fd);
      conn->backend_fd_ = -1;
    } else {
      PLOG(ERROR) << "read from backend";
      delete conn;
    }
    return;
  }

  // NOTE: we don't need to handle responses in emebedded mode
  // bool forwarded = false;
  // if (!conn->lite_core_.emergency_mode_ptr_->load()) {
  //   forwarded = true;
  //   if (!network::Write(conn->client_fd_, conn->buffer_, bytes_transferred))
  //   {
  //     LOG(ERROR) << "Failed to write request to backend" << std::endl;
  //     delete conn;
  //     return;
  //   }
  // }
  //
  // uint8_t* begin = conn->buffer_;
  // uint8_t* end = begin + bytes_transferred;
  // while (begin != end) {
  //   const auto result = conn->response_->Deserialize(begin, end);
  //   if (result == kGood) {
  //     if (!conn->lite_core_.HandleResponse(
  //             boost::move(conn->response_),
  //             conn->connection_state_entry_ptr_->extra_app_info_,
  //             conn->connection_state_entry_ptr_->pending_requests_,
  //             conn->client_fd_, &conn->connection_state_entry_ptr_->cache_,
  //             forwarded)) {
  //       delete conn;
  //       return;
  //     }
  //     conn->response_ = ShmMakeShared(
  //         conn->lite_core_.shared_memory_.get_segment_manager()
  //             ->template construct<Response>(bip::anonymous_instance)(
  //                 conn->lite_core_.shared_memory_.get_segment_manager()),
  //         conn->lite_core_.shared_memory_);
  //   } else if (result == kIndeterminate) {
  //     continue;
  //   } else if (result == kBad) {
  //     LOG(ERROR) << "failed to parse response" << std::endl;
  //     return;
  //   }
  // }
  return;
}

template <typename Application, typename Request, typename Response,
          typename ConnectionInfo, typename CacheKey, typename CacheEntry>
bool Connection<Application, Request, Response, ConnectionInfo, CacheKey,
                CacheEntry>::ConnectBackend() {
  // Set up a socket connection to the backend server
  if ((backend_fd_ = network::TryConnectBackend(
           lite_core_.backend_addr_, lite_core_.backend_port_)) == -1) {
    return false;
  }

  if (client_fd_ > 0) {
    auto tcp_id = network::GetTCPID(client_fd_);
    lite_core_.connection_state_storage_ptr_->replay_conns_.push_back(tcp_id);
  } else {
    auto tcp_id = network::TCPID::GetUUID();
    lite_core_.connection_state_storage_ptr_->replay_conns_.push_back(tcp_id);
    connection_state_entry_ptr_ =
        lite_core_.connection_state_storage_ptr_->GetOrAdd(tcp_id);
  }

  // remove previous event
  if (backend_event_.ev_base) event_del(&backend_event_);

  // Add an event that listens to the backend server's messages
  event_set(&backend_event_, backend_fd_, EV_READ | EV_PERSIST,
            Connection::BackendHandler, static_cast<void*>(this));
  event_base_set(base_, &backend_event_);
  if (event_add(&backend_event_, 0) == -1) {
    PLOG(ERROR) << "backend event_add";
    throw std::runtime_error("backend event_add");
  }

  return true;
}

template <typename Application, typename Request, typename Response,
          typename ConnectionInfo, typename CacheKey, typename CacheEntry>
void Connection<Application, Request, Response, ConnectionInfo, CacheKey,
                CacheEntry>::Detach() {
  if (client_fd_ > 0) client_fd_ = 0;
  if (client_event_.ev_base) event_del(&client_event_);
}

}  // namespace lite