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
      cache_(lite_core.cache_inner_, lite_core.logger_inner_, log_head_),
      logger_(lite_core.logger_inner_, log_head_),
      worker_ptr_(worker_ptr) {
  if (sfd) {
    AttachToWorker(sfd, event_flags, base, event_handler, worker_ptr);
  } else {
    memset(&client_event_, 0, sizeof(client_event_));
  }

  memset(&backend_event_, 0, sizeof(backend_event_));

  if (is_client_connection &&
      (!lite_core_.emergency_mode_ && !lite_core_.is_replaying_ &&
       !lite_core_.is_ebpf_))
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
                CacheEntry>::AttachToWorker(const evutil_socket_t sfd,
                                            const int event_flags,
                                            struct event_base* base,
                                            EventHandler event_handler,
                                            WorkerInstance* worker_ptr) {
  client_fd_ = sfd;
  base_ = base;
  worker_ptr_ = worker_ptr;

  event_set(&client_event_, sfd, event_flags, event_handler,
            static_cast<void*>(this));
  event_base_set(base, &client_event_);
  if (event_add(&client_event_, 0) == -1) {
    PLOG(ERROR) << "client event_add";
    throw std::runtime_error("client event_add");
  }

  if (worker_ptr_) worker_ptr_->conns_.insert(this);
}

template <typename Application, typename Request, typename Response,
          typename ConnectionInfo, typename CacheKey, typename CacheEntry>
void Connection<Application, Request, Response, ConnectionInfo, CacheKey,
                CacheEntry>::DetachFromWorker() {
  if (client_event_.ev_base) event_del(&client_event_);
  if (backend_event_.ev_base) event_del(&backend_event_);
  if (worker_ptr_) {
    worker_ptr_->conns_.erase(this);
    worker_ptr_ = nullptr;
  }
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
              &conn->cache_, &conn->logger_, forwarded)) {
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

// Request update
template <typename Application, typename Request, typename Response,
          typename ConnectionInfo, typename CacheKey, typename CacheEntry>
void Connection<Application, Request, Response, ConnectionInfo, CacheKey,
                CacheEntry>::RequestUpdate(uint8_t* buffer, int len,
                                           uint32_t seq_num) {
  // std::stringstream packet_content;
  // for (size_t i = 0; i < len; i++) {
  //   char c = buffer[i];
  //   if (c != '\r' && c != '\n') {
  //     packet_content << c;
  //   } else if (c == '\r') {
  //     packet_content << "\\r";
  //   } else if (c == '\n') {
  //     packet_content << "\\n";
  //   }
  // }
  // LOG(INFO) << "RequestUpdate: packet_content: " << packet_content.str() <<
  // std::endl;

  if (seq_num != expected_seq_num_) {
    LOG(ERROR) << "RequestUpdate: seq_num mismatch. Expected "
               << expected_seq_num_ << " but got " << seq_num
               << " this: " << this << std::endl;
  }
  // else {
  //   LOG(INFO) << "RequestUpdate: seq_num matched. Expected " <<
  //   expected_seq_num_
  //             << " got " << seq_num << std::endl;
  // }
  expected_seq_num_ = seq_num + 1;
  request_num_ = seq_num;
  // if (request_num_ != response_num_ + 1 || to_be_closed_) {
  //   LOG(ERROR) << "RequestUpdate: request_num mismatch. Expected " <<
  //   response_num_ + 1
  //              << " but got " << request_num_ << " this: " << this <<
  //              std::endl;
  //   for (size_t i = 0; i < 3; i++) {
  //     std::cout << "Last request buffer " << i << ": ";
  //     for (size_t j = 0; j < last_request_buffer_size_[i]; j++) {
  //       std::cout << last_request_buffer_[i][j];
  //     }
  //     std::cout << std::endl;
  //   }
  //   std::cout << "Request buffer: ";
  //   for (size_t i = 0; i < len; i++) {
  //     std::cout << buffer[i];
  //   }
  //   std::cout << std::endl;
  //   std::cout << std::endl;
  //   // if (to_be_closed_ == 6) exit(1);
  //   to_be_closed_++;
  // }
  // for (size_t i = 0; i < 2; i++) {
  //   last_request_buffer_size_[i] = last_request_buffer_size_[i+1];
  //   memcpy(last_request_buffer_[i], last_request_buffer_[i+1],
  //   last_request_buffer_size_[i+1]);
  // }
  // last_request_buffer_size_[2] = len;
  // memcpy(last_request_buffer_[2], buffer, len);
  bool forwarded = false;
  // check if the buffer is large enough
  if (len > 131072) {
    LOG(ERROR) << "RequestUpdate: buffer is too large" << std::endl;
    return;
  }
  uint8_t* begin = buffer;
  uint8_t* end = begin + len;
  while (begin != end) {
    const auto result = request_->Deserialize(begin, end);
    if (result == kGood) {
      if (!lite_core_.HandleRequest(std::move(request_), extra_app_info_,
                                    pending_requests_, client_fd_, backend_fd_,
                                    &cache_, &logger_, forwarded)) {
        return;
      }
      request_ = std::make_unique<Request>();
    } else if (result == kIndeterminate) {
      continue;
    } else if (result == kBad) {
      LOG(ERROR) << "failed to parse request" << std::endl;
      return;
    }
  }
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

  // NOTE: we don't need to handle responses in eBPF mode
  // bool forwarded = false;
  // if (!conn->lite_core_.emergency_mode_) {
  //   forwarded = true;
  //   if (!network::Write(conn->client_fd_, conn->buffer_, bytes_transferred)) {
  //     LOG(ERROR) << "Failed to write request to backend" << std::endl;
  //     delete conn;
  //     return;
  //   }
  // }

  // uint8_t* begin = conn->buffer_;
  // uint8_t* end = begin + bytes_transferred;
  // while (begin != end) {
  //   const auto result = conn->response_->Deserialize(begin, end);
  //   if (result == kGood) {
  //     if (!conn->lite_core_.HandleResponse(
  //             std::move(conn->response_), conn->extra_app_info_,
  //             conn->pending_requests_, conn->client_fd_, &conn->cache_,
  //             forwarded)) {
  //       delete conn;
  //       return;
  //     }
  //     conn->response_ = std::make_unique<Response>();
  //   } else if (result == kIndeterminate) {
  //     continue;
  //   } else if (result == kBad) {
  //     LOG(ERROR) << "failed to parse response" << std::endl;
  //     return;
  //   }
  // }
  return;
}
// Response update
template <typename Application, typename Request, typename Response,
          typename ConnectionInfo, typename CacheKey, typename CacheEntry>
void Connection<Application, Request, Response, ConnectionInfo, CacheKey,
                CacheEntry>::ResponseUpdate(uint8_t* buffer, int len,
                                            uint32_t seq_num) {
  // std::stringstream packet_content;
  // for (size_t i = 0; i < len; i++) {
  //   char c = buffer[i];
  //   if (c != '\r' && c != '\n') {
  //     packet_content << c;
  //   } else if (c == '\r') {
  //     packet_content << "\\r";
  //   } else if (c == '\n') {
  //     packet_content << "\\n";
  //   }
  // }
  // LOG(INFO) << "ResponseUpdate: packet_content: " << packet_content.str() <<
  // std::endl;

  if (seq_num != expected_seq_num_) {
    LOG(ERROR) << "ResponseUpdate: seq_num mismatch. Expected "
               << expected_seq_num_ << " but got " << seq_num
               << " this: " << this << std::endl;
  }
  // else {
  //   LOG(INFO) << "ResponseUpdate: seq_num matched. Expected " <<
  //   expected_seq_num_
  //             << " got " << seq_num << std::endl;
  // }
  expected_seq_num_ = seq_num + 1;
  response_num_ = seq_num;
  // if (response_num_ != request_num_ + 1 || to_be_closed_) {
  //   LOG(ERROR) << "ResponseUpdate: response_num mismatch. Expected " <<
  //   request_num_ + 1
  //              << " but got " << response_num_ << " this: " << this <<
  //              std::endl;
  //   for (size_t i = 0; i < 3; i++) {
  //     std::cout << "Last response buffer " << i << ": ";
  //     for (size_t j = 0; j < last_response_buffer_size_[i]; j++) {
  //       std::cout << last_response_buffer_[i][j];
  //     }
  //     std::cout << std::endl;
  //   }
  //   std::cout << "Response buffer: ";
  //   for (size_t i = 0; i < len; i++) {
  //     std::cout << buffer[i];
  //   }
  //   std::cout << std::endl;
  //   std::cout << std::endl;
  //   // if (to_be_closed_ == 6) exit(1);
  //   to_be_closed_++;
  // }
  // for (size_t i = 0; i < 2; i++) {
  //   last_response_buffer_size_[i] = last_response_buffer_size_[i+1];
  //   memcpy(last_response_buffer_[i], last_response_buffer_[i+1],
  //   last_response_buffer_size_[i+1]);
  // }
  // last_response_buffer_size_[2] = len;
  // memcpy(last_response_buffer_[2], buffer, len);
  bool forwarded = false;
  if (len > 131072) {
    LOG(ERROR) << "ResponseUpdate: buffer is too large" << std::endl;
    return;
  }
  uint8_t* begin = buffer;
  uint8_t* end = begin + len;
  while (begin != end) {
    const auto result = response_->Deserialize(begin, end);
    if (result == kGood) {
      if (!lite_core_.HandleResponse(std::move(response_), extra_app_info_,
                                     pending_requests_, client_fd_, &cache_,
                                     forwarded)) {
        return;
      }
      response_ = std::make_unique<Response>();
    } else if (result == kIndeterminate) {
      continue;
    } else if (result == kBad) {
      LOG(ERROR) << "failed to parse response" << std::endl;
      return;
    }
  }
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