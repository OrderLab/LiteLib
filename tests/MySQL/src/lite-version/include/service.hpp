#pragma once

#include <lite.hpp>

#include "dissect.hpp"
#include "packet.hpp"

struct ConnectionInfo {
  ResponseDissector response_dissector;
  std::vector<std::shared_ptr<Packet>> responses;
  std::unordered_map<uint32_t, std::string> prepared_statements;
};

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

 private:
  bool ParseQueryCache();
};