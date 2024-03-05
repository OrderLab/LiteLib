#pragma once

#include <string>

#include "cache.hpp"

namespace memcached {
namespace server {

struct Packet;

/// The common handler for all incoming requests.
class RequestHandler {
 public:
  RequestHandler& operator=(const RequestHandler&) = delete;

  explicit RequestHandler(const size_t& max_item_count)
      : cache_(max_item_count) {
    kNotFound_ = {'N', 'o', 't', ' ', 'F', 'o', 'u', 'n', 'd'};
  };

  /// Handle a request and produce a response.
  void HandleRequest(const Packet& req, std::vector<Packet>& responses,
                     bool& is_quit, bool& is_quite);

 private:
  /// The content of the cache
  Cache cache_;
  std::vector<uint8_t> kNotFound_;
};

}  // namespace server
}  // namespace memcached
