#pragma once

#include <event.h>

#include <memory>
#include <vector>

#include "bip.hpp"

namespace lite {

namespace network {
evutil_socket_t TryConnectBackend(const std::string& addr,
                                  const std::string& port);

[[nodiscard]] bool Write(const evutil_socket_t fd, const uint8_t buffer[],
                         size_t len);

[[nodiscard]] bool Write(const evutil_socket_t fd,
                         const std::vector<uint8_t> buffer, size_t len);

[[nodiscard]] bool Write(const evutil_socket_t fd,
                         const std::vector<uint8_t>&& buffer);

[[nodiscard]] bool Write(const evutil_socket_t fd,
                         const std::unique_ptr<std::vector<uint8_t>> buffer);

[[nodiscard]] bool Write(const evutil_socket_t fd,
                         const std::shared_ptr<std::vector<uint8_t>> buffer);

[[nodiscard]] bool Write(const evutil_socket_t fd,
                         const ShmSharedPtr<ShmVector<uint8_t>> buffer);

struct TCPID {
  uint32_t src_ip;
  uint16_t src_port;
  uint32_t dst_ip;
  uint16_t dst_port;
};

TCPID GetTCPID(const evutil_socket_t fd);

}  // namespace network

}  // namespace lite