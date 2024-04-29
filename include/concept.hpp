#pragma once

#include <concepts>
#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <optional>
#include <vector>

namespace lite {

enum DeserializeResult { kGood, kBad, kIndeterminate };

template <typename CacheKey, typename CacheEntry>
concept IsCacheEntry = requires(CacheEntry entry, CacheKey key) {
  {
    entry.ToRequests(key)
  } -> std::convertible_to<std::shared_ptr<std::vector<uint8_t>>>;
};

template <typename T1, typename T2, typename T3>
class Logger;

template <typename T1, typename T2, typename T3>
class Cache;

template <typename Application, typename Request, typename Response,
          typename ConnectionInfo, typename CacheKey, typename CacheEntry>
concept IsApplication = requires(
    Application app, std::shared_ptr<Request> req,
    std::shared_ptr<Response> resp, ConnectionInfo conn_info,
    std::deque<std::shared_ptr<Request>> pending_requests,
    Cache<CacheKey, CacheEntry, Request> *cache,
    Logger<Request, CacheKey, CacheEntry> *logger) {
  // Find the corresponding requests of the response, return a subset of the
  // requests that contain information about state changes
  {
    app.Filter(resp, conn_info, pending_requests)
  }
  -> std::convertible_to<std::optional<std::vector<std::shared_ptr<Request>>>>;

  // Update the states during normal time
  {
    app.NormalUpdate(
        resp, std::move(app.Filter(resp, conn_info, pending_requests).value()),
        conn_info, cache)
  };

  // Perform any operation during emergency time
  {
    app.EmergencyServe(std::move(req), conn_info, cache, logger)
  } -> std::convertible_to<Response>;
};

template <typename ProtocolMessage>
concept IsProtocolMessage =
    requires(ProtocolMessage m, uint8_t *&begin, uint8_t *end) {
      {
        m.Serialize()
      } -> std::convertible_to<std::shared_ptr<std::vector<uint8_t>>>;

      { m.Deserialize(begin, end) } -> std::convertible_to<DeserializeResult>;
    };

}  // namespace lite