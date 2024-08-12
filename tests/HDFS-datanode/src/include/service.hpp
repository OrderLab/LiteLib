#pragma once

#include <boost/chrono.hpp>
#include <boost/thread.hpp>
#include <functional>
#include <lite.hpp>

#include "DatanodeProtocol.pb.h"
#include "IpcConnectionContext.pb.h"
#include "ProtobufRpcEngine.pb.h"
#include "RpcHeader.pb.h"
#include "datatransfer.pb.h"
#include "packet.hpp"

namespace std {
    template <>
    struct hash<std::pair<std::string, long>> {
        std::size_t operator()(const std::pair<std::string, long>& k) const {
            // Combine the hash values of the two members
            return std::hash<std::string>()(k.first) ^ std::hash<long>()(k.second);
        }
    };
}

struct ConnectionInfo {};

struct CacheEntry {
  std::shared_ptr<Packet> ToRequest(const std::string &key) const {
    return std::make_shared<Packet>();
  }
};

class Datanode {
  using Cache = lite::Cache<Datanode, Packet, Packet, ConnectionInfo,
                            std::string, CacheEntry>;
  using Logger = lite::Logger<Datanode, Packet, Packet, ConnectionInfo,
                              std::string, CacheEntry>;
  std::atomic<bool> emergency = false;

  boost::thread *HeartbeatThread;

  // used to identify the Other type.
  std::shared_ptr<Packet> LastResponse = std::make_shared<Packet>();
  // TODO: the cache is temporary
  hadoop::hdfs::datanode::HeartbeatRequestProto HeartbeatRequest;
  hadoop::common::RpcRequestHeaderProto rpc_request_header;
  hadoop::common::RequestHeaderProto requestHeaderProto;

  std::unordered_map<std::pair<std::string, long>, hadoop::hdfs::ExtendedBlockProto> BlockMap;

  lite::LiteServer<Datanode, Packet, Packet, ConnectionInfo, std::string,
                   CacheEntry> *server;
  bool initialized = false;
 public:
  std::pair<std::vector<std::shared_ptr<Packet>>, lite::RequestType> Match(
      std::shared_ptr<Packet> &resp, ConnectionInfo &conn,
      lite::ThreadSafeQueue<std::pair<std::shared_ptr<Packet>,
                                      lite::RequestType>> &pending_requests);

  void NormalUpdate(const std::shared_ptr<Packet> &resp,
                    std::vector<std::shared_ptr<Packet>> requests,
                    ConnectionInfo &conn, Cache *cache);

  void HandleReplayResponse(const std::shared_ptr<Packet> &resp,
                            std::vector<std::shared_ptr<Packet>> requests,
                            ConnectionInfo &conn, Cache *cache) {
    return;
  }
  void HandleCustomizedResponse(const std::shared_ptr<Packet> &resp,
                                std::vector<std::shared_ptr<Packet>> requests,
                                ConnectionInfo &conn, Cache *cache) {}
  std::pair<Packet, bool> EmergencyServe(std::shared_ptr<Packet> req,
                                         ConnectionInfo &conn, Cache *cache,
                                         Logger *logger, bool flow_control);
  void NormalToEmergencyHook();
  void EmergencyToNormalHook();
  Packet EmergencyConnectionEstablishHook(ConnectionInfo conn_info) {
    return Packet{};
  }
  void SendHeartbeat();

  void RegisterServer(lite::LiteServer<Datanode, Packet, Packet, ConnectionInfo,
                                       std::string, CacheEntry> *liteserver) {
    server = liteserver;
  }
};