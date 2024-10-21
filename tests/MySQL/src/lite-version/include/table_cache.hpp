#pragma once

#include <hsql/SQLParser.h>

#include <lite.hpp>
#include <string>
#include <vector>

#include "dissect.hpp"
#include "query_cache.hpp"

struct CacheKey {
  std::string table;
  std::vector<Value> primary_keys;

  bool operator==(const CacheKey &rhs) const {
    return table == rhs.table && primary_keys == rhs.primary_keys;
  }

  friend std::size_t hash_value(const CacheKey &self) {
    std::size_t seed = 0;

    boost::hash_combine(seed, self.table);
    for (const auto &key : self.primary_keys) {
      boost::hash_combine(seed, key);
    }

    return seed;
  }
};

struct CacheEntry {
  std::vector<std::optional<Value>> values;

  size_t GetSize() const {
    // TODO
    return values.size();
  }

  std::shared_ptr<Packet> ToRequest(const CacheKey &key) const {
    // TODO
    return std::make_shared<Packet>();
  }
};

struct TableSchema {
  struct TableColumn {
    Type type;
    bool is_primary_key;
  };
  size_t primary_keys_size, values_size;
  std::vector<TableColumn>
      columns;  // primary key 1, primary key 2, value 1, value 2, ...
  std::unordered_map<std::string, size_t> columns_name_to_index;
};

class MySQL;

class TableCache {
  using Cache =
      lite::Cache<MySQL, Packet, Packet, ConnectionInfo, CacheKey, CacheEntry>;

 public:
  TableCache();

  // if update_query_cache is false, only invalidate related entries
  bool HandleInsert(const hsql::InsertStatement &stmt, Cache *cache,
                    QueryCache *query_cache, bool update_query_cache = true);

  bool HandleDelete(const hsql::DeleteStatement &stmt, Cache *cache,
                    QueryCache *query_cache, bool update_query_cache = true);

  bool HandleUpdate(const hsql::UpdateStatement &stmt, Cache *cache,
                    QueryCache *query_cache, bool update_query_cache = true);

  std::optional<Packet> ServePointSelect(const hsql::SelectStatement &stmt,
                                         Cache *cache);

  friend class QueryCache;

 private:
  std::unordered_map<std::string, TableSchema> tables_;

  std::optional<bool> WhereMatch(const CacheKey &key, const CacheEntry &entry,
                                 const hsql::Expr *where);

  void UpdateQueryCache(const CacheKey &key, const CacheEntry *old_entry,
                        const CacheEntry *new_entry, QueryCache *query_cache,
                        bool update_query_cache);
};