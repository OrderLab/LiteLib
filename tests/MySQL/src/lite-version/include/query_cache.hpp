#pragma once

#include <hsql/SQLParser.h>
#include <hsql/util/sqlhelper.h>

#include <optional>
#include <unordered_map>

#include "dissect.hpp"
#include "mysql-server/sql_cache.hpp"
#include "packet.hpp"
#include "query_cache_range_index.hpp"

class MySQL;
class TableCache;
class CacheKey;
class CacheEntry;

class Result {
  std::vector<uint8_t>
      prefix_packets;  // column count, field packet, intermediate EOF
  std::vector<uint8_t> suffix_packets;  // EOF packet except packet length and
                                        // number, and response code

 public:
  Result() = default;
  Result(Result &&rhs)
      : prefix_packets(std::move(rhs.prefix_packets)),
        suffix_packets(std::move(rhs.suffix_packets)),
        rows(std::move(rhs.rows)) {}
  // Result &operator=(Result &&rhs) {
  //   if (this != &rhs) {
  //     prefix_packets = std::move(rhs.prefix_packets);
  //     suffix_packets = std::move(rhs.suffix_packets);
  //     rows = std::move(rhs.rows);
  //   }
  //   return *this;
  // }

  using Row = std::vector<Value>;

  std::vector<Row> rows;

  static Result Deserialize(std::vector<uint8_t> &buffer,
                            const hsql::SelectStatement *stmt);

  std::shared_ptr<std::vector<uint8_t>> Serialize();
};

// TODO: separate different columns
class QueryAndResult {
 public:
  QueryAndResult()
      : mutex_ptr(std::make_unique<std::shared_mutex>()),
        select_statement(nullptr) {}
  QueryAndResult(Result &&result, hsql::SQLParserResult &&query_ast)
      : result(std::move(result)),
        query_ast(std::move(query_ast)),
        mutex_ptr(std::make_unique<std::shared_mutex>()),
        select_statement(nullptr) {}
  // QueryAndResult &operator=(QueryAndResult &&rhs) {
  //   if (this != &rhs) {
  //     query_ast = std::move(rhs.query_ast);
  //     result = std::move(rhs.result);
  //     mutex_ptr = std::move(rhs.mutex_ptr);
  //   }
  //   return *this;
  // }

  const hsql::SelectStatement *GetSelectStatement() const {
    if (select_statement) {
      return select_statement;
    }
    return select_statement = dynamic_cast<const hsql::SelectStatement *>(
               query_ast.getStatement(0));
  }
  std::string GetWhereClause() const {
    std::stringstream where_stream;
    if (GetSelectStatement()->whereClause != nullptr)
      printExpression(where_stream, GetSelectStatement()->whereClause, 0);
    return where_stream.str();
  }

  std::unique_ptr<std::shared_mutex> mutex_ptr;

  Result result;

 private:
  hsql::SQLParserResult query_ast;
  mutable const hsql::SelectStatement *select_statement;
};

class QueryCache {
  using Cache =
      lite::Cache<MySQL, Packet, Packet, ConnectionInfo, CacheKey, CacheEntry>;

 private:
  class WhereQueryCache {
   public:
    WhereQueryCache() = default;
    WhereQueryCache(WhereQueryCache &&rhs)
        : query_and_results(std::move(rhs.query_and_results)) {}
    boost::unordered::concurrent_flat_map<std::string,
                                          std::unique_ptr<QueryAndResult>>
        query_and_results;  // key: query string
  };

  class TableQueryCache {
   public:
    TableQueryCache() = default;
    TableQueryCache(TableQueryCache &&rhs)
        : where_query_caches(std::move(rhs.where_query_caches)) {}
    // TODO: use structural where clause as key
    boost::unordered::concurrent_flat_map<std::string, WhereQueryCache>
        where_query_caches;  // key: serialized where expr

    boost::unordered::concurrent_flat_map<std::string,
                                          std::unique_ptr<QueryCacheRangeIndex>>
        column_range_indices;  // key: column name
  };

  int GetSizeAndDump(bool dump = false);

 public:
  QueryCache(MySQL &mysql);

  ~QueryCache();

  void EmergencyToNormalHook();

  bool NormalToEmergencyHook(TableCache &table_cache, Cache *cache);

  std::optional<Packet> ServeSelect(const std::string &query);

 private:
  MySQL &mysql_;

  int shm_fd_;

  size_t shm_size_;

  uchar *shm_ptr_;

  AlignedShmInfo *shm_info_;

  ptrdiff_t shm_v_offset_;

  boost::unordered::concurrent_flat_map<std::string, TableQueryCache>
      table_query_caches_;  // key: table name

  void ConnectToFull();

  void DisconnectFromFull();

  void AddQueryCacheBlock(Query_cache_block *query_cache_block_lite_ptr,
                          TableCache &table_cache, Cache *cache);

  void AddQueryAndResult(std::string query, std::vector<uint8_t> &result,
                         TableCache &table_cache, Cache *cache);

  void BuildRelationsBetweenQueryAndCachedRows();

  friend class TableCache;

 public:  // listener
  void HandleInvalidatedQueryBlockFromFull(
      Query_cache_block *query_cache_block_full_ptr, TableCache &table_cache,
      Cache *cache);

  void InvalidateUnprocessableDeleteDuringNormal(
      const hsql::DeleteStatement *stmt, TableCache &table_cache);

  void InvalidateUnprocessableUpdateDuringNormal(
      const hsql::UpdateStatement *stmt, TableCache &table_cache);

 private:
  int full_to_lite_fd_, lite_to_full_fd_;

  pthread_t full_listener_thread_;

  struct event_base *full_listener_base_;

  struct event full_listener_event_;

  bool SendQueryToFull(Query_cache_block *query_cache_block_full_ptr);

  static void *FullListenerThreadBody(void *arg_self);

  static void FullListenerHandler(int fd, short which, void *arg_self);
};