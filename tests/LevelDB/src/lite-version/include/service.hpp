#pragma once

#include <event.h>

#include <core.hpp>
#include <memory>
#include <string>

#include "connection.hpp"
#include "packet.hpp"

struct CacheEntry {
  std::shared_ptr<std::string> value = nullptr;
  size_t GetSize() const { return (value ? value->size() : 0); }
};

struct LogEntry {
  std::shared_ptr<Packet> value;

  std::shared_ptr<std::vector<uint8_t>> Serialize() const {
    return value->Serialize();
  }

  lite::DeserializeResult Deserialize(uint8_t *&begin, uint8_t *end) {
    return value->Deserialize(begin, end);
  }

  std::shared_ptr<std::vector<uint8_t>> ToPacket() const {
    return value->Serialize();
  }
};

class LevelDB {
  using Connection =
      lite::Connection<Packet, LevelDB, std::string, CacheEntry, LogEntry>;
  using Cache = lite::Cache<std::string, CacheEntry>;
  using Logger = lite::Logger<LogEntry>;

 public:
  LevelDB(std::string &backend_addr, std::string &backend_port);

  bool Filter(const std::shared_ptr<Packet> &p, Connection &conn) const;

  void NormalUpdate(const std::shared_ptr<Packet> &p, Connection &conn,
                    Cache &cache);

  void EmergencyServe(std::shared_ptr<Packet> p, Connection &conn, Cache &cache,
                      Logger &logger);

  void Replay(Logger &logger);

  static void BackendHandler(evutil_socket_t fd, short which, void *arg_conn);

 private:
  std::string &backend_addr_, &backend_port_;

  void NormalUpdateImpl(const std::shared_ptr<Packet> &p, Cache &cache,
                        const bool in_transaction = false);

  RESPType *EmergencyServeImpl(std::shared_ptr<Packet> p, Connection &conn,
                               Cache &cache, Logger &logger,
                               const bool in_transaction = false);
};
