#pragma once

#include <event.h>

#include <lite.hpp>
#include <memory>
#include <string>

#include "packet.hpp"

struct CacheEntry {
  std::shared_ptr<std::vector<uint8_t>> value = nullptr;
  std::shared_ptr<std::vector<uint8_t>> flags = nullptr;

  size_t GetSize() const {
    return (value ? value->size() : 0) + (flags ? flags->size() : 0);
  }

  std::shared_ptr<Packet> ToRequest(const std::vector<uint8_t> &key) const {
    auto req = std::make_shared<Packet>();
    const auto size_str = std::to_string(value->size());
    req->buffer->reserve(key.size() + flags->size() + value->size() +
                         size_str.size() + 11);
    req->buffer->push_back('s');
    req->buffer->push_back('e');
    req->buffer->push_back('t');
    req->buffer->push_back(' ');
    req->buffer->insert(req->buffer->end(), key.begin(), key.end());
    req->buffer->push_back(' ');
    req->buffer->insert(req->buffer->end(), flags->begin(), flags->end());
    req->buffer->push_back('0');
    req->buffer->push_back(' ');
    req->buffer->insert(req->buffer->end(), size_str.begin(), size_str.end());
    req->buffer->push_back('\r');
    req->buffer->push_back('\n');
    req->buffer->insert(req->buffer->end(), value->begin(), value->end());
    req->buffer->push_back('\r');
    req->buffer->push_back('\n');
    return req;
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
  Memcached();

  std::pair<std::vector<std::shared_ptr<Packet>>, bool> Match(
      const std::shared_ptr<Packet> &resp, ConnectionInfo &_,
      lite::ThreadSafeQueue<std::pair<std::shared_ptr<Packet>, bool>>
          &pending_requests) const;

  void NormalUpdate(const std::shared_ptr<Packet> &resp,
                    std::vector<std::shared_ptr<Packet>> requests,
                    ConnectionInfo &_, Cache *cache);

  void HandleReplayResponse(const std::shared_ptr<Packet> &resp,
                            std::vector<std::shared_ptr<Packet>> requests,
                            ConnectionInfo &_, Cache *cache) const;

  std::pair<Packet, bool> EmergencyServe(std::shared_ptr<Packet> p,
                                         ConnectionInfo &conn_info,
                                         Cache *cache, Logger *logger,
                                         bool flow_control) const;

  std::pair<Packet, bool> EmergencyServeImpl(std::shared_ptr<Packet> p,
                                             Cache *cache, Logger *logger,
                                             bool flow_control) const;

  void NormalToEmergencyHook() {}

  void EmergencyToNormalHook() {}

  std::optional<Packet> EmergencyConnectionEstablishHook(
      ConnectionInfo &conn_info) {
    return std::nullopt;
  }

 private:
  Packet stored, not_stored, null_resp, version;
};
