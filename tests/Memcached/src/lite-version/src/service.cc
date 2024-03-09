#include "service.hpp"

#include "connection.hpp"

MemcachedService::MemcachedService(const size_t &max_item_count)
    : cache_(max_item_count) {}

bool MemcachedService::Filter(const std::unique_ptr<Packet> &p) const {
  return false;
}

void MemcachedService::NormalUpdate(std::unique_ptr<Packet> p) {}

void MemcachedService::NormalForwardAndProxyBack(
    std::unique_ptr<Packet> p, char _, const evutil_socket_t server_fd) const {
  write(server_fd, p->buffer->data(), p->buffer->size());
}

void MemcachedService::EmergencyServe(std::unique_ptr<Packet> p) {}

void MemcachedService::Replay() {}

void MemcachedService::BackendHandler(evutil_socket_t fd, short which,
                                      void *arg_conn) {
  auto conn = static_cast<Connection *>(arg_conn);
  std::unique_ptr<std::vector<uint8_t>> buffer =
      std::make_unique<std::vector<uint8_t>>(16384);
  const ssize_t bytes_transferred =
      read(conn->backend_fd_, buffer->data(), 16384);
  if (bytes_transferred <= 0) {
    // TODO: handle this
    perror("read from backend");
    return;
  }
  buffer->resize(bytes_transferred);
  conn->Write(std::move(buffer));
}