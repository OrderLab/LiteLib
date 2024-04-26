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

  std::shared_ptr<std::vector<uint8_t>> ToRequests(
      const std::vector<uint8_t> &key) const {
    ParsedPacket req;
    static std::vector<uint8_t> expiry(4, 0);  // TODO: use real one
    req.header.magic = 0x80;
    req.header.opcode = magic_enum::enum_underlying(Header::Opcode::kSetQ);
    req.key = std::make_shared<std::vector<uint8_t>>(key);
    req.value = value;
    req.extra = flags;
    req.header.CAS = CAS;
    req.header.extras_length = 8;
    req.header.key_length = req.key->size();
    req.header.total_body_length =
        req.value->size() + req.header.key_length + req.header.extras_length;
    req.buffer->clear();
    auto buffer = req.Serialize();
    buffer->insert(buffer->begin() + 28, expiry.begin(), expiry.end());
    return buffer;
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
                        Cache &cache, std::function<void(LogEntry)> log_func,
                        std::function<bool(size_t)> undo_log_func) const;
};
