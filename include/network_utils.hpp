#pragma once

#include <event.h>

#include <memory>
#include <vector>

namespace lite {

namespace network {
evutil_socket_t TryConnectBackend(const std::string& addr,
                                  const std::string& port);

void Write(const evutil_socket_t fd, const std::vector<uint8_t> buffer,
           size_t len);

void Write(const evutil_socket_t fd, const std::vector<uint8_t>&& buffer);

void Write(const evutil_socket_t fd,
           const std::unique_ptr<std::vector<uint8_t>> buffer);

void Write(const evutil_socket_t fd,
           const std::shared_ptr<std::vector<uint8_t>> buffer);

}  // namespace network

}  // namespace lite