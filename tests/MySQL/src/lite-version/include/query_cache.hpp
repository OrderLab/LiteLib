#pragma once

#include <hsql/SQLParser.h>

#include <optional>
#include <unordered_map>

#include "dissect.hpp"
#include "mysql-server/sql_cache.hpp"
#include "packet.hpp"

class MySQL;
class TableCache;
class CacheKey;
class CacheEntry;

class QueryCache {
  using Cache =
      lite::Cache<MySQL, Packet, Packet, ConnectionInfo, CacheKey, CacheEntry>;

 private:
  class Result {
    std::vector<uint8_t>
        prefix_packets;  // column count, field packet, intermediate EOF
    std::vector<uint8_t> suffix_packets;  // EOF packet except packet length and
                                          // number, and response code

   public:
    using Row = std::vector<Value>;

    std::vector<Row> rows;

    static Result Deserialize(std::vector<uint8_t> &buffer,
                              const hsql::SelectStatement *stmt);

    std::shared_ptr<std::vector<uint8_t>> Serialize();
  };

  // TODO: separate different columns
  class ResultTableEntry {
   public:
    Result result;

    ResultTableEntry() {}
    ResultTableEntry(Result &&result, hsql::SQLParserResult &&query_ast)
        : result(std::move(result)), query_ast(std::move(query_ast)) {}
    ResultTableEntry &operator=(ResultTableEntry &&rhs) {
      if (this != &rhs) {
        query_ast = std::move(rhs.query_ast);
        result = std::move(rhs.result);
      }
      return *this;
    }

    const hsql::SelectStatement *GetSelectStatement() {
      if (select_statement) {
        return select_statement;
      }
      return select_statement = dynamic_cast<const hsql::SelectStatement *>(
                 query_ast.getStatement(0));
    }

   private:
    hsql::SQLParserResult query_ast;
    const hsql::SelectStatement *select_statement = nullptr;
  };

  using ResultTable =
      std::unordered_map<std::string,
                         ResultTableEntry>;  // key: query string

  // TODO: use structural where clause as key
  using TableQueryCache = std::unordered_map<std::string,
                                             ResultTable>;  // key: serialized
                                                            // where expr

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

  std::unordered_map<std::string, TableQueryCache>
      query_cache_;  // key: table name

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

  bool SendQueryToFull(Query_cache_block *query_cache_block_full_ptr);

 private:
  int full_to_lite_fd_, lite_to_full_fd_;

  pthread_t full_listener_thread_;

  struct event_base *full_listener_base_;

  struct event full_listener_event_;

  static void *FullListenerThreadBody(void *arg_self);

  static void FullListenerHandler(int fd, short which, void *arg_self);
};