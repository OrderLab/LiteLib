#pragma once

#include <lite.hpp>

#include "packet.hpp"

extern SharedMemory *shm;

struct ConnectionInfo {
  ConnectionInfo(ShmVoidAllocator allocator) {}
};

using CacheKey = ShmString;

struct CacheEntry {
  ShmString value;

  CacheEntry(ShmVoidAllocator allocator) : value(allocator) {}

  size_t GetSize() const { return 1; }
  std::shared_ptr<Packet> ToRequest(const std::string &key) const {
    return std::make_shared<Packet>(
        shm->get_segment_manager()->get_allocator<uint8_t>());
  }
};

class Proxy {
  using Cache =
      lite::Cache<Proxy, Packet, Packet, ConnectionInfo, CacheKey, CacheEntry>;
  using Logger =
      lite::Logger<Proxy, Packet, Packet, ConnectionInfo, CacheKey, CacheEntry>;

 public:
  std::pair<std::vector<ShmSharedPtr<Packet>>, bool> Match(
      const ShmSharedPtr<Packet> &resp, ConnectionInfo &conn,
      lite::ShmThreadSafeQueue<bip::pair<ShmSharedPtr<Packet>, bool>>
          &pending_requests) {
    pending_requests.clear();
    return {std::vector<ShmSharedPtr<Packet>>{}, true};
  }

  void NormalUpdate(const ShmSharedPtr<Packet> &resp,
                    std::vector<ShmSharedPtr<Packet>> requests,
                    ConnectionInfo &conn, Cache *cache) {
    return;
  }

  void HandleReplayResponse(const ShmSharedPtr<Packet> &resp,
                            std::vector<ShmSharedPtr<Packet>> requests,
                            ConnectionInfo &conn, Cache *cache) {
    return;
  }

  std::pair<Packet, bool> EmergencyServe(ShmSharedPtr<Packet> req,
                                         ConnectionInfo &conn, Cache *cache,
                                         Logger *logger, bool flow_control) {
    return {Packet{shm->get_segment_manager()->get_allocator<uint8_t>()},
            true};  // close the connection directly
  }

  void NormalToEmergencyHook() {}

  void EmergencyToNormalHook() {}

  Packet EmergencyConnectionEstablishHook(ConnectionInfo _) {
    return Packet{shm->get_segment_manager()->get_allocator<uint8_t>()};
  }
};