#pragma once

#include <lite.hpp>
#include <optional>

#include "dissect.hpp"
#include "packet.hpp"
#include "query_cache.hpp"
#include "table_cache.hpp"
#include "worker.hpp"

class MySQL {
  using Cache =
      lite::Cache<MySQL, Packet, Packet, ConnectionInfo, CacheKey, CacheEntry>;
  using Logger =
      lite::Logger<MySQL, Packet, Packet, ConnectionInfo, CacheKey, CacheEntry>;

 public:
  MySQL(const size_t &number_of_workers);

  ~MySQL();

  std::pair<std::vector<std::shared_ptr<Packet>>, bool> Match(
      const std::shared_ptr<Packet> &resp, ConnectionInfo &conn,
      lite::ThreadSafeQueue<std::pair<std::shared_ptr<Packet>, bool>>
          &pending_requests);

  void NormalUpdate(const std::shared_ptr<Packet> &resp,
                    std::vector<std::shared_ptr<Packet>> requests,
                    ConnectionInfo &conn, std::shared_ptr<Cache> cache);

  void HandleReplayResponse(const std::shared_ptr<Packet> &resp,
                            std::vector<std::shared_ptr<Packet>> requests,
                            ConnectionInfo &conn, std::shared_ptr<Cache> cache);

  std::pair<Packet, bool> EmergencyServe(std::shared_ptr<Packet> req,
                                         ConnectionInfo &conn, std::shared_ptr<Cache> cache,
                                         Logger *logger, bool flow_control);

  void NormalToEmergencyHook();

  void EmergencyToNormalHook();

  Packet EmergencyConnectionEstablishHook(ConnectionInfo &conn);

  Packet ReplayConnectionEstablishHook(ConnectionInfo &conn);

  Cache *dangling_cache_;  // used by workers

  void AssignNewNormalTask(NormalTask &&task);

 private:
  Packet server_greeting_, login_request_;

  TableCache table_cache_;

  QueryCache query_cache_;

  void NormalUpdateQuery(std::string &query, ConnectionInfo *conn,
                         Cache *cache);

  std::pair<Packet, bool> EmergencyServeQuery(std::shared_ptr<Packet> req,
                                              std::string &query,
                                              ConnectionInfo &conn,
                                              Cache *cache,
                                              Logger *logger,
                                              bool flow_control);

  friend class MySQLWorker;

  std::vector<MySQLWorker *> workers_in_normal_;
  std::vector<MySQLWorker *>::iterator cur_worker = workers_in_normal_.begin();
  std::mutex cur_worker_mutex;
};