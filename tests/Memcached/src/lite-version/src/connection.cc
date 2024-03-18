#include "connection.hpp"

#include <vector>

Connection::Connection(const evutil_socket_t sfd, const int event_flags,
                       struct event_base* base, EventHandler event_handler,
                       MemcachedLiteServer& lite_server,
                       bool is_client_connection,
                       std::string &backend_addr,
                       std::string &backend_port)
    : base_(base),
      backend_addr_(backend_addr),
      backend_port_(backend_port),
      client_fd_(sfd),
      request_(std::make_unique<Packet>()),
      lite_server_(lite_server),
      response_buffer_(std::make_unique<std::vector<uint8_t>>()) {
  event_set(&client_event_, sfd, event_flags, event_handler,
            static_cast<void*>(this));
  event_base_set(base, &client_event_);
  if (event_add(&client_event_, 0) == -1) {
    perror("event_add");
    throw std::runtime_error("event_add");
  }

  if (is_client_connection) ConnectBackend();
}

evutil_socket_t Connection::TryConnectBackend(const std::string& addr,
                                              const std::string& port) {
  evutil_socket_t backend_fd;
  struct addrinfo hints, *res;

  memset(&hints, 0, sizeof(hints));
  hints.ai_family = AF_UNSPEC;
  hints.ai_socktype = SOCK_STREAM;

  if (getaddrinfo(addr.c_str(), port.c_str(), &hints, &res) !=
      0) {
    perror("getaddrinfo");
    return -1;
  }

  bool connected = false;
  for (; res; res = res->ai_next) {
    backend_fd = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    if (backend_fd == -1) {
      // perror("failed to connect backend");
      // goto connect_backend_exit;
      continue;
    }

    if (connect(backend_fd, res->ai_addr, res->ai_addrlen) == -1) {
      // perror("failed to connect backend");
      // goto connect_backend_exit;
      continue;
    }

    connected = true;
    break;
  }

  if (!connected) {
    perror("failed to connect backend");
    goto connect_backend_exit;
  }

  return backend_fd;

connect_backend_exit:
  freeaddrinfo(res);
  return -1;
}

bool Connection::ConnectBackend() {
  // Set up a socket connection to the backend server
  if ((backend_fd_ = TryConnectBackend(backend_addr_, backend_port_)) == -1) {
    return false;
  }

  // Add an event that listens to the backend server's messages
  event_set(&backend_event_, backend_fd_, EV_READ | EV_PERSIST,
            MemcachedService::BackendHandler, static_cast<void*>(this));
  event_base_set(base_, &backend_event_);
  if (event_add(&backend_event_, 0) == -1) {
    perror("event_add");
    throw std::runtime_error("event_add");
  }

  return true;
}

Connection::~Connection() {
  /* delete the event, the socket and the conn */
  close(backend_fd_);
  close(client_fd_);
  event_del(&client_event_);
  event_del(&backend_event_);
  // std::cerr << "connection closed" << std::endl;
  return;
}

int Connection::Accept() {
  socklen_t addrlen;
  struct sockaddr_storage addr;
  addrlen = sizeof(addr);
  return accept4(client_fd_, (struct sockaddr*)&addr, &addrlen, SOCK_NONBLOCK);
}

#define unlikely(x) __builtin_expect((x), 0)
bool Connection::Read() {
  ssize_t bytes_transferred;
  if (unlikely((bytes_transferred = read(client_fd_, buffer_.data(), 16384)) <=
               0)) {
    // connection closed or error
    return false;
  }
  // // std::cerr << "Connection::AsyncRead: " << e.message() << std::endl;
  uint8_t* begin = buffer_.data();
  for (;;) {
    const auto old_begin = begin;
    auto result =
        request_parser_.Parse(begin, buffer_.data() + bytes_transferred);
    request_->buffer->insert(request_->buffer->end(), old_begin, begin);
    if (result == SimpleParser::kGood) {
      lite_server_.Serve(std::move(request_), this, backend_fd_);
      request_ = std::make_unique<Packet>();
      request_parser_.Reset();
      // requests might be coalesced
    } else if (result == SimpleParser::kBad) {
      // TODO: error handling
      std::cerr << "bad client request:\n" << request_ << std::endl;
      break;
    } else {
      // RequestParser::kIndeterminate
      break;
    }
  }
  return true;
}

void Connection::FlushBuffer() {
  write(client_fd_, response_buffer_->data(), response_buffer_->size());
  response_buffer_->clear();
}

void Connection::Write(std::unique_ptr<std::vector<uint8_t>> buffer) {
  // TODO: async to reduce latency
  // TODO: use transmit in the full version
  write(client_fd_, buffer->data(), buffer->size());
}
