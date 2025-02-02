#pragma once

#include <lite.hpp>

#include "packet.hpp"

struct ConnectionInfo {};

using SharedString =
    bip::basic_string<char, std::char_traits<char>, SharedAllocator<char>>;
using CacheKey = SharedString;

struct CacheEntry {
  SharedString value;

  CacheEntry(SegmentManager *segment_mgr) : value(segment_mgr) {}

  size_t GetSize() const { return 1; }
  std::shared_ptr<Packet> ToRequest(const std::string &key) const {
    return std::make_shared<Packet>();
  }
};

class Proxy {
  using Cache =
      lite::Cache<Proxy, Packet, Packet, ConnectionInfo, CacheKey, CacheEntry>;
  using Logger =
      lite::Logger<Proxy, Packet, Packet, ConnectionInfo, CacheKey, CacheEntry>;

 public:
  std::pair<std::vector<std::shared_ptr<Packet>>, bool> Match(
      const std::shared_ptr<Packet> &resp, ConnectionInfo &conn,
      lite::ThreadSafeQueue<std::pair<std::shared_ptr<Packet>, bool>>
          &pending_requests) {
    pending_requests.clear();
    return {std::vector<std::shared_ptr<Packet>>{}, true};
  }

  void NormalUpdate(const std::shared_ptr<Packet> &resp,
                    std::vector<std::shared_ptr<Packet>> requests,
                    ConnectionInfo &conn, Cache *cache) {
    return;
  }

  void HandleReplayResponse(const std::shared_ptr<Packet> &resp,
                            std::vector<std::shared_ptr<Packet>> requests,
                            ConnectionInfo &conn, Cache *cache) {
    return;
  }

  std::pair<Packet, bool> EmergencyServe(std::shared_ptr<Packet> req,
                                         ConnectionInfo &conn, Cache *cache,
                                         Logger *logger, bool flow_control) {
    return {Packet{}, true};  // close the connection directly
  }

  void NormalToEmergencyHook() {}

  void EmergencyToNormalHook() {}

  Packet EmergencyConnectionEstablishHook(ConnectionInfo _) {}
};