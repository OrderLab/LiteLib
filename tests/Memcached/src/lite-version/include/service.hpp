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

  std::shared_ptr<std::vector<uint8_t>> ToRequests() {
    return packet->Serialize();
  }
};

struct ConnectionInfo {
  std::unique_ptr<std::vector<uint8_t>> response_buffer;
  ConnectionInfo()
      : response_buffer(std::make_unique<std::vector<uint8_t>>()) {}
};

class Memcached {
  using Cache = lite::Cache<std::vector<uint8_t>, CacheEntry>;
  using Logger = lite::Logger<LogEntry>;

 public:
  std::optional<std::vector<std::shared_ptr<Packet>>> Filter(
      const std::shared_ptr<Packet> &resp, ConnectionInfo &_,
      std::deque<std::shared_ptr<Packet>> &pending_requests) const;

  void NormalUpdate(const std::shared_ptr<Packet> &resp,
                    std::vector<std::shared_ptr<Packet>> requests,
                    ConnectionInfo &_, Cache &cache) const;

  Packet EmergencyServe(std::shared_ptr<Packet> p, ConnectionInfo &conn_info,
                        Cache &cache, Logger &logger) const;
};
