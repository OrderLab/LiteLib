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

template <typename T1, typename T2, typename T3, typename T4, typename T5,
          typename T6>
class Logger;

template <typename T1, typename T2, typename T3, typename T4, typename T5,
          typename T6>
class Cache;

template <typename Application, typename Request, typename Response,
          typename ConnectionInfo, typename CacheKey, typename CacheEntry>
concept IsApplication = requires(
    Application app, std::shared_ptr<Request> req,
    std::shared_ptr<Response> resp, ConnectionInfo conn_info,
    std::deque<std::pair<std::shared_ptr<Request>, bool>>
        pending_requests,  // true: request forward from client, false:
                           // request generated during replay
    std::vector<std::shared_ptr<Request>> related_requests,
    Cache<Application, Request, Response, ConnectionInfo, CacheKey, CacheEntry>
        *cache,
    Logger<Application, Request, Response, ConnectionInfo, CacheKey, CacheEntry>
        *logger) {
  // Find the corresponding requests of the response, return a subset of the
  // requests that contain information about state changes
  {
    app.Match(resp, conn_info, pending_requests)
  } -> std::convertible_to<std::pair<std::vector<std::shared_ptr<Request>>,
                                     bool>>;  // pair<related_requests,
                                              // forward response>

  // Update the states during normal time
  { app.NormalUpdate(resp, related_requests, conn_info, cache) };

  // Handle response of requests sent by Replay (those Match() = (_, false))
  // TODO: let the application to be able to retry the request
  { app.HandleReplayResponse(resp, related_requests, conn_info, cache) };

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