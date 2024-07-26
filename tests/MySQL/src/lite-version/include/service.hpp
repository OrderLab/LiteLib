#pragma once

#include <lite.hpp>

#include "dissect.hpp"
#include "packet.hpp"

struct CacheEntry {
  std::shared_ptr<Packet> ToRequest(const std::string &key) const {
    return std::make_shared<Packet>();
  }
};

class MySQL {
  using Cache = lite::Cache<MySQL, Packet, Packet, ConnectionInfo, std::string,
                            CacheEntry>;
  using Logger = lite::Logger<MySQL, Packet, Packet, ConnectionInfo,
                              std::string, CacheEntry>;

 public:
  MySQL();

  std::pair<std::vector<std::shared_ptr<Packet>>, bool> Match(
      const std::shared_ptr<Packet> &resp, ConnectionInfo &conn,
      lite::ThreadSafeQueue<std::pair<std::shared_ptr<Packet>, bool>>
          &pending_requests);

  void NormalUpdate(const std::shared_ptr<Packet> &resp,
                    std::vector<std::shared_ptr<Packet>> requests,
                    ConnectionInfo &conn, Cache *cache);

  void HandleReplayResponse(const std::shared_ptr<Packet> &resp,
                            std::vector<std::shared_ptr<Packet>> requests,
                            ConnectionInfo &conn, Cache *cache);

  std::pair<Packet, bool> EmergencyServe(std::shared_ptr<Packet> req,
                                         ConnectionInfo &conn, Cache *cache,
                                         Logger *logger, bool flow_control);

  void NormalToEmergencyHook();

  void EmergencyToNormalHook() {}

  Packet EmergencyConnectionEstablishHook(ConnectionInfo &conn);

 private:
  Packet server_greeting_;

  std::unordered_map<std::string, std::shared_ptr<std::vector<uint8_t>>>
      query_cache_;

  bool ParseQueryCache();

  std::pair<Packet, bool> EmergencyServeQuery(std::string &query,
                                              ConnectionInfo &conn,
                                              Cache *cache, Logger *logger,
                                              bool flow_control);
};