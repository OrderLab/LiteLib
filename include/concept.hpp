#pragma once

#include <concepts>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <vector>

#include "shm_thread_safe_queue.hpp"

namespace lite {

typedef void (*RequestDestructorFn)(void *request);

#define unreachable __builtin_unreachable()
#define likely(x) __builtin_expect(!!(x), 1)
#define unlikely(x) __builtin_expect(!!(x), 0)

enum DeserializeResult { kGood, kBad, kIndeterminate };

template <typename CacheKey>
concept IsCacheKey = requires(CacheKey key, ShmVoidAllocator allocator) {
  {
    CacheKey(allocator)
  };  // It must be entirely self-contained within the shared memory
};

template <typename Request, typename CacheKey, typename CacheEntry>
concept IsCacheEntry =
    requires(CacheEntry entry, CacheKey key, ShmVoidAllocator allocator) {
      { entry.ToRequest(key) } -> std::convertible_to<ShmSharedPtr<Request>>;
      {
        CacheEntry(allocator)
      };  // It must be entirely self-contained within the shared memory
    };

template <typename ConnectionInfo>
concept IsConnectionInfo =
    requires(ConnectionInfo conn_info, ShmVoidAllocator allocator) {
      {
        ConnectionInfo(allocator)
      };  // It must be entirely self-contained within the shared memory
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
    Application app, ShmSharedPtr<Request> req, ShmSharedPtr<Response> resp,
    ConnectionInfo conn_info,
    ShmThreadSafeQueue<std::pair<ShmSharedPtr<Request>, bool>>
        pending_requests,  // true: request forward from client, false:
                           // request generated during replay
    std::vector<ShmSharedPtr<Request>> related_requests,
    Cache<Application, Request, Response, ConnectionInfo, CacheKey, CacheEntry>
        *cache,
    Logger<Application, Request, Response, ConnectionInfo, CacheKey, CacheEntry>
        *logger,
    bool flow_control,  // true: reject this request if it will trigger
                        // replay packets
    void *none_type_req, RequestDestructorFn request_destructor_fn) {
  // Update the states during normal time
  {
    Application::EmbeddedNormalUpdate(none_type_req, conn_info, cache,
                                      request_destructor_fn)
  } -> std::convertible_to<int>;

  // Perform any operation during emergency time
  {
    app.EmergencyServe(req, conn_info, cache, logger, flow_control)
  } -> std::convertible_to<std::pair<Response, bool>>;  // true: close the
                                                        // connection after
                                                        // sending the response

  // Hook function for switching from normal to emergency mode
  { app.NormalToEmergencyHook() };

  // Hook function for switching from emergency to normal mode
  { app.EmergencyToNormalHook() };

  // Hook function for establishing emergency connection
  {
    app.EmergencyConnectionEstablishHook(conn_info)
  } -> std::convertible_to<std::optional<Response>>;
};

template <typename ProtocolMessage>
concept IsProtocolMessage =
    requires(ProtocolMessage m, uint8_t *&begin, uint8_t *end) {
      {
        m.Serialize()
      } -> std::convertible_to<ShmSharedPtr<ShmVector<uint8_t>>>;

      { m.Deserialize(begin, end) } -> std::convertible_to<DeserializeResult>;
    };

}  // namespace lite