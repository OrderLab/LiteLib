#pragma once

#include <lite.hpp>
#include <optional>

#include "dissect.hpp"
#include "packet.hpp"
#include "query_cache.hpp"
#include "table_cache.hpp"

class MySQL {
  using Cache =
      lite::Cache<MySQL, Packet, Packet, ConnectionInfo, CacheKey, CacheEntry>;
  using Logger =
      lite::Logger<MySQL, Packet, Packet, ConnectionInfo, CacheKey, CacheEntry>;

 public:
  MySQL();

  ~MySQL();

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

  void EmergencyToNormalHook();

  Packet EmergencyConnectionEstablishHook(ConnectionInfo &conn);

 private:
  Packet server_greeting_;

  TableCache table_cache_;

  QueryCache query_cache_;

  void NormalUpdateQuery(std::string &query, ConnectionInfo *conn,
                         Cache *cache);

  std::pair<Packet, bool> EmergencyServeQuery(std::string &query,
                                              ConnectionInfo &conn,
                                              Cache *cache, Logger *logger,
                                              bool flow_control);

 public:  // normal task queue
  struct NormalTask {
    enum class Type {
      kInsertCache,
      kUpdateQuery,
    } type;

    Query_cache_block *query_cache_block_full_ptr;

    std::string query;
    ConnectionInfo
        *conn;  // TODO: what if the connection is closed before it is handled?
    Cache *cache;
  };

  evutil_socket_t notify_event_fd_;

  lite::ThreadSafeQueue<NormalTask> notify_queue_;

 private:
  pthread_t thread_id_;

  struct event_base *base_;

  struct event notify_event_;

  static void *ThreadBody(void *arg_self);

  static void NotifyHandler(evutil_socket_t fd, short which, void *arg_self);
};