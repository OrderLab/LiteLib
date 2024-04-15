#pragma once

#include <event.h>

#include <concept.hpp>
#include <memory>
#include <string>

#include "packet.hpp"

struct CacheEntry {
  std::shared_ptr<std::vector<uint8_t>> value = nullptr;
  std::shared_ptr<std::vector<uint8_t>> flags = nullptr;
  uint64_t CAS;
  size_t GetSize() const {
    return (value ? value->size() : 0) + (flags ? flags->size() : 0) +
           sizeof(CAS);
  }
};

struct LogEntry {  // TODO: deal with expiry
  std::shared_ptr<Packet> packet;

  std::shared_ptr<std::vector<uint8_t>> Serialize() {
    return packet->Serialize();
  }

  lite::DeserializeResult Deserialize(uint8_t *&begin, uint8_t *end) {
    return packet->Deserialize(begin, end);
  }

  std::shared_ptr<std::vector<uint8_t>> ToPacket() {
    return packet->Serialize();
  }
};

struct ConnectionInfo {
  std::unique_ptr<std::vector<uint8_t>> response_buffer;
};

class Memcached {
  using Cache = lite::Cache<std::vector<uint8_t>, CacheEntry>;
  using Logger = lite::Logger<LogEntry>;

 public:
  bool Filter(const std::shared_ptr<Packet> &p, ConnectionInfo &_) const;

  void NormalUpdate(const std::shared_ptr<Packet> &p, ConnectionInfo &_,
                    Cache &cache);

  Packet EmergencyServe(std::shared_ptr<Packet> p, ConnectionInfo &_,
                        Cache &cache, Logger &logger);

  void Replay();
};
