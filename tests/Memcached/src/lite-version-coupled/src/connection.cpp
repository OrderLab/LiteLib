#include "connection.hpp"

#include <vector>

#include "request_handler.hpp"

namespace memcached {
namespace server {

Connection::Connection(const evutil_socket_t sfd, const int event_flags,
                       struct event_base* base, EventHandler event_handler,
                       RequestHandler& request_handler)
    : fd_(sfd), request_handler_(request_handler) {
  request_.value = std::make_shared<std::vector<uint8_t>>();

  event_set(&event_, sfd, event_flags, event_handler, static_cast<void*>(this));
  event_base_set(base, &event_);
  if (event_add(&event_, 0) == -1) {
    perror("event_add");
    throw std::runtime_error("event_add");
  }
}

Connection::~Connection() {
  /* delete the event, the socket and the conn */
  event_del(&event_);
  return;
}

int Connection::Accept() {
  socklen_t addrlen;
  struct sockaddr_storage addr;
  addrlen = sizeof(addr);
  return accept4(fd_, (struct sockaddr*)&addr, &addrlen, SOCK_NONBLOCK);
}

#define unlikely(x) __builtin_expect((x),0)
bool Connection::Read() {
  int bytes_transferred;
  if (unlikely((bytes_transferred = read(fd_, buffer_.data(), 16384)) <= 0)) {
    // connection closed or error
    return false;
  }
  // // std::cerr << "Connection::AsyncRead: " << e.message() << std::endl;
  uint8_t* begin = buffer_.data();
  for (;;) {
    auto result = request_parser_.Parse(request_, begin,
                                        buffer_.data() + bytes_transferred);

    if (result == RequestParser::kGood) {
      bool is_quit, is_quiet;
      request_handler_.HandleRequest(request_, responses_, is_quit, is_quiet);
      if (!is_quiet) {
        // std::cerr << "Responses
        // sent--------------------------------------------" << std::endl;
        std::unique_ptr<std::vector<uint8_t>> buffer_ptr =
            std::make_unique<std::vector<uint8_t>>();
        std::vector<uint8_t> buffer;
        for (auto& resp : responses_) resp.ToBuffers(*buffer_ptr);
        responses_.clear();
        Write(std::move(buffer_ptr));
        // boost::asio::async_write(
        //     socket_, boost::asio::buffer(*buffer_ptr.get()),
        //     boost::bind(&Connection::Write, shared_from_this(),
        //                 boost::asio::placeholders::error, buffer_ptr));
      }
      if (is_quit) break;
      request_.header = Header();
      request_.extra.clear();
      request_.key.clear();
      request_.value = std::make_shared<std::vector<uint8_t>>();
      request_parser_.Reset();
    } else if (result == RequestParser::kBad) {
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

void Connection::Write(std::unique_ptr<std::vector<uint8_t>> buffer) {
  // TODO: async to reduce latency
  // TODO: use transmit in the full version
  write(fd_, buffer->data(), buffer->size());
}

}  // namespace server
}  // namespace memcached
