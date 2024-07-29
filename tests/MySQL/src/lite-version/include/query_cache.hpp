#pragma once

#include <hsql/SQLParser.h>

#include <optional>
#include <unordered_map>

#include "dissect.hpp"
#include "packet.hpp"

class QueryCache {
 private:
  class Result {
    std::vector<uint8_t>
        prefix_packets;  // column count, field packet, intermediate EOF
    std::vector<uint8_t> suffix_packets;  // EOF packet except packet length and
                                          // number, and response code

    uint8_t null_bitmap_length;

   public:
    using Row = std::vector<Value>;

    std::vector<Row> rows;

    static Result Deserialize(std::vector<uint8_t> &buffer);

    std::shared_ptr<std::vector<uint8_t>> Serialize();
  };

  // TODO: separate different columns
  struct ResultTableEntry {
    const hsql::SelectStatement *select;
    Result result;
  };

  using ResultTable =
      std::unordered_map<std::string,
                         ResultTableEntry>;  // key: query string

  struct TableQueryCacheEntry {
    hsql::Expr *where;  // TODO: garbage collection
    std::shared_ptr<ResultTable> result_table;
  };

  // TODO: use structural where clause as key
  using TableQueryCache =
      std::unordered_map<std::string,
                         TableQueryCacheEntry>;  // key: serialized
                                                 // where expr

 public:
  bool Init();

  std::optional<Packet> ServeSelect(const std::string &query);

 private:
  std::unordered_map<std::string, TableQueryCache>
      query_cache_;  // key: table name

  void AddQueryAndResult(std::string query, std::vector<uint8_t> &result);

  void BuildRelationsBetweenQueryAndCachedRows();

  friend class TableCache;
};