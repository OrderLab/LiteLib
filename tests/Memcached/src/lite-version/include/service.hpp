#pragma once

#include <event.h>

#include <lite.hpp>
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

  std::shared_ptr<Packet> ToRequest(const std::vector<uint8_t> &key) const {
    auto req = std::make_shared<ParsedPacket>();
    static std::vector<uint8_t> expiry(4, 0);  // TODO: use real one
    req->header.magic = 0x80;
    req->header.opcode = magic_enum::enum_underlying(Header::Opcode::kSetQ);
    req->key = std::make_shared<std::vector<uint8_t>>(key);
    req->value = value;
    req->extra = flags;
    req->header.CAS = CAS;
    req->header.extras_length = 8;
    req->header.key_length = req->key->size();
    req->header.total_body_length =
        req->value->size() + req->header.key_length + req->header.extras_length;
    req->buffer->clear();
    return req;
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
};

struct ConnectionInfo {
  std::unique_ptr<std::vector<uint8_t>> response_buffer;
  ConnectionInfo()
      : response_buffer(std::make_unique<std::vector<uint8_t>>()) {}
};

class Memcached {
  using Cache = lite::Cache<Memcached, Packet, Packet, ConnectionInfo,
                            std::vector<uint8_t>, CacheEntry>;
  using Logger = lite::Logger<Memcached, Packet, Packet, ConnectionInfo,
                              std::vector<uint8_t>, CacheEntry>;

 public:
  std::pair<std::vector<std::shared_ptr<Packet>>, bool> Match(
      const std::shared_ptr<Packet> &resp, ConnectionInfo &_,
      std::deque<std::pair<std::shared_ptr<Packet>, bool>> &pending_requests)
      const;

  void NormalUpdate(const std::shared_ptr<Packet> &resp,
                    std::vector<std::shared_ptr<Packet>> requests,
                    ConnectionInfo &_, Cache *cache) const;

  void HandleReplayResponse(const std::shared_ptr<Packet> &resp,
                            std::vector<std::shared_ptr<Packet>> requests,
                            ConnectionInfo &_, Cache *cache) const;

  Packet EmergencyServe(std::shared_ptr<Packet> p, ConnectionInfo &conn_info,
                        Cache *cache, Logger *logger) const;
};
