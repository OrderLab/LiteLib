#pragma once

#include <event.h>

#include <boost/functional/hash.hpp>
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

  bool operator==(const TCPID& other) const {
    return src_ip == other.src_ip && src_port == other.src_port &&
           dst_ip == other.dst_ip && dst_port == other.dst_port;
  }
};

TCPID GetTCPID(const evutil_socket_t fd);

}  // namespace network

}  // namespace lite

namespace boost {
template <>
struct hash<lite::network::TCPID> {
  std::size_t operator()(const lite::network::TCPID& id) const {
    std::size_t seed = 0;
    boost::hash_combine(seed, id.src_ip);
    boost::hash_combine(seed, id.src_port);
    boost::hash_combine(seed, id.dst_ip);
    boost::hash_combine(seed, id.dst_port);
    return seed;
  }
};
}  // namespace boost