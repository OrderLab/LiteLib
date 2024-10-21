#pragma once
#include "lite.hpp"

#include "packet.hpp"
#include "dissect.hpp"
#include "query_cache.hpp"

class MySQL;

struct NormalTask {
  using Cache =
      lite::Cache<MySQL, Packet, Packet, ConnectionInfo, CacheKey, CacheEntry>;

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

class MySQLWorker {
 public:
  MySQLWorker(MySQL &mysql);

  ~MySQLWorker();

  evutil_socket_t notify_event_fd_;

  lite::ThreadSafeQueue<NormalTask> notify_queue_;

 private:
  MySQL &mysql_;

  pthread_t thread_id_;

  struct event_base *base_;

  struct event notify_event_;

  static void *ThreadBody(void *arg_self);

  static void NotifyHandler(evutil_socket_t fd, short which, void *arg_self);
};