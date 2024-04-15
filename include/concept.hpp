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

template <typename Application, typename Packet, typename Connection,
          typename CacheKey, typename CacheEntry, typename LogEntry>
concept IsApplication =
    requires(Application app, std::shared_ptr<Packet> p, Connection conn,
             Cache<CacheKey, CacheEntry> &cache, Logger<LogEntry> &l) {
      // Whether it's an operation that contains state info and thus needs to be
      // cached e.g. UPDATE -> true, READ -> false
      { app.Filter(p, conn) } -> std::convertible_to<bool>;

      // Perform the cachable operation during normal time
      { app.NormalUpdate(p, conn, cache) };

      // Perform any operation during emergency time
      {
        app.EmergencyServe(std::move(p), conn, cache, l)
      } -> std::convertible_to<Packet>;
    } &&
    !requires(Application app, std::shared_ptr<Packet> p, Connection conn,
              Cache<CacheKey, CacheEntry> &cache, Logger<LogEntry> &l) {
      {
        app.NormalUpdate(p, conn, std::move(cache))
      };  // cache must be a reference
      {
        app.EmergencyServe(std::move(p), conn, std::move(cache), l)
      };  // cache must be a reference
      {
        app.EmergencyServe(std::move(p), conn, cache, std::move(l))
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