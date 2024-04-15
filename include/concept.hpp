#pragma once

#include <concepts>
#include <cstdint>
#include <functional>
#include <memory>
#include <vector>

namespace lite {

enum DeserializeResult { kGood, kBad, kIndeterminate };

template <typename LogEntry>
concept IsLogEntry =
    requires(LogEntry log_entry, uint8_t *&begin, uint8_t *end) {
      {
        log_entry.Serialize()
      } -> std::convertible_to<std::shared_ptr<std::vector<uint8_t>>>;

      {
        log_entry.Deserialize(begin, end)
      } -> std::convertible_to<DeserializeResult>;

      {
        log_entry.ToPacket()
      } -> std::convertible_to<std::shared_ptr<std::vector<uint8_t>>>;
    } && !requires(LogEntry log_entry, uint8_t *&begin, uint8_t *end) {
      {
        log_entry.Deserialize(std::move(begin), end)
      };  // begin must be a reference
    };

template <typename T>
  requires IsLogEntry<T>
class Logger;

template <typename T1, typename T2>
class Cache;

template <typename Application, typename Packet, typename ConnectionInfo,
          typename CacheKey, typename CacheEntry, typename LogEntry>
concept IsApplication =
    requires(Application app, std::shared_ptr<Packet> p,
             ConnectionInfo conn_info, Cache<CacheKey, CacheEntry> &cache,
             Logger<LogEntry> &logger) {
      // Whether it's an operation that contains state info and thus needs to be
      // cached e.g. UPDATE -> true, READ -> false
      { app.Filter(p, conn_info) } -> std::convertible_to<bool>;

      // Perform the cachable operation during normal time
      { app.NormalUpdate(p, conn_info, cache) };

      // Perform any operation during emergency time
      {
        app.EmergencyServe(std::move(p), conn_info, cache, logger)
      } -> std::convertible_to<Packet>;
    } &&
    !requires(Application app, std::shared_ptr<Packet> p,
              ConnectionInfo conn_info, Cache<CacheKey, CacheEntry> &cache,
              Logger<LogEntry> &logger) {
      {
        app.NormalUpdate(p, conn_info, std::move(cache))
      };  // cache must be a reference
      {
        app.EmergencyServe(std::move(p), conn_info, std::move(cache), logger)
      };  // cache must be a reference
      {
        app.EmergencyServe(std::move(p), conn_info, cache, std::move(logger))
      };  // l must be a reference
    };

template <typename Packet>
concept IsPacket = requires(Packet p, uint8_t *&begin, uint8_t *end) {
  {
    p.Serialize()
  } -> std::convertible_to<std::shared_ptr<std::vector<uint8_t>>>;

  { p.Deserialize(begin, end) } -> std::convertible_to<DeserializeResult>;
} && !requires(Packet p, uint8_t *&begin, uint8_t *end) {
  { p.Deserialize(std::move(begin), end) };  // begin must be a reference
};

}  // namespace lite