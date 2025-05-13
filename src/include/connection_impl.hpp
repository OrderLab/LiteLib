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
                                   WorkerInstance* worker_ptr)
    : base_(base),
      client_fd_(sfd),
      backend_fd_(-1),
      request_(std::make_unique<Request>()),
      response_(std::make_unique<Response>()),
      lite_server_(lite_server),
      lite_core_(lite_core),
      self_(std::make_shared<ConnectionInstance*>(this)),
      log_head_(new LogEntryInstance(nullptr, nullptr, self_)),
      cache_(std::make_shared<CacheInstance>(lite_core.cache_inner_, lite_core.logger_inner_, log_head_)),
      logger_(lite_core.logger_inner_, log_head_),
      worker_ptr_(worker_ptr) {
  if (sfd) {
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

  if (is_client_connection &&
      (!lite_core_.emergency_mode_ && !lite_core_.is_replaying_))
    ConnectBackend();

  if (lite_core_.emergency_mode_) {
    std::optional<Response> greeting_msg =
        lite_core_.app_.EmergencyConnectionEstablishHook(extra_app_info_);
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

  lite_core_.dead_connection_log_heads_.push_back(log_head_);
  // LOG(INFO) << "connection closed" << std::endl;
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
      PLOG(ERROR) << "read from client";
    delete conn;
    // TODO: how to properly handle the case when the client disconnects as
    // expected? (e.g. quit command in Memcached)
    return;
  }

  bool forwarded = false;
  if (!conn->lite_core_.emergency_mode_) {
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
      if (conn->backend_fd_ <= 0 && !conn->lite_core_.emergency_mode_) {
        conn->ConnectBackend();
      }
      if (!conn->lite_core_.HandleRequest(
              std::move(conn->request_), conn->extra_app_info_,
              conn->pending_requests_, conn->client_fd_, conn->backend_fd_,
              conn->cache_, &conn->logger_, forwarded)) {
        delete conn;
        return;
      }
      conn->request_ = std::make_unique<Request>();
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

  bool forwarded = false;
  if (!conn->lite_core_.emergency_mode_) {
    forwarded = true;
    if (!network::Write(conn->client_fd_, conn->buffer_, bytes_transferred)) {
      LOG(ERROR) << "Failed to write request to backend" << std::endl;
      delete conn;
      return;
    }
  }

  uint8_t* begin = conn->buffer_;
  uint8_t* end = begin + bytes_transferred;
  while (begin != end) {
    const auto result = conn->response_->Deserialize(begin, end);
    if (result == kGood) {
      if (!conn->lite_core_.HandleResponse(
              std::move(conn->response_), conn->extra_app_info_,
              conn->pending_requests_, conn->client_fd_, conn->cache_,
              forwarded)) {
        delete conn;
        return;
      }
      conn->response_ = std::make_unique<Response>();
    } else if (result == kIndeterminate) {
      continue;
    } else if (result == kBad) {
      LOG(ERROR) << "failed to parse response" << std::endl;
      return;
    }
  }
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

}  // namespace lite