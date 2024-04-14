#pragma once

#include <concepts>
#include <cstdint>

namespace lite {

template <typename LogEntry>
concept IsLogEntry = requires(LogEntry log_entry) {
  { log_entry.Serialize() } -> std::convertible_to<std::vector<uint8_t>>;
};

template <typename T>
  requires IsLogEntry<T>
class Logger;

template <typename T1, typename T2>
class Cache;

template <typename Application, typename Packet, typename Connection,
          typename CacheKey, typename CacheEntry, typename LogEntry>
concept IsApplication =
    requires(Application app, Packet p, Connection conn,
             Cache<CacheKey, CacheEntry> &cache, Logger<LogEntry> &l) {
      // Whether it's an operation that contains state info and thus needs to be
      // cached e.g. UPDATE -> true, READ -> false
      { app.Filter(p, conn) } -> std::convertible_to<bool>;

      // Perform the cachable operation during normal time
      { app.NormalUpdate(p, conn, cache) };

      // Perform any operation during emergency time
      { app.EmergencyServe(std::move(p), conn, cache, l) };
    };

enum DeserializeResult { kGood, kBad, kIndeterminate };

template <typename Packet>
concept IsPacket = requires(Packet p, uint8_t *&begin, uint8_t *end) {
  { p.Deserialize(begin, end) } -> std::convertible_to<DeserializeResult>;
};
}  // namespace lite