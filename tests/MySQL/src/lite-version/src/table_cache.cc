#include "table_cache.hpp"

#include <hsql/util/sqlhelper.h>

#include <algorithm>
#include <sstream>

#include "service.hpp"

TableCache::TableCache() {
  // TODO: read from backend
  TableSchema schema;
  schema.primary_keys_size = 1;
  schema.values_size = 3;
  schema.columns = {
      {kLL, true}, {kLL, false}, {kVARCHAR, false}, {kVARCHAR, false}};
  schema.columns_name_to_index = {{"id", 0}, {"k", 1}, {"c", 2}, {"pad", 3}};

  tables_["sbtest1"] = schema;
  tables_["sbtest2"] = schema;
}

bool TableCache::HandleInsert(const hsql::InsertStatement &stmt, Cache *cache,
                              QueryCache *query_cache) {
  if (stmt.type != hsql::kInsertValues) {
    // TODO
    std::stringstream ss;
    hsql::printStatementInfo(ss, &stmt);
    LOG(WARNING) << "Unsupported insert type:" << std::endl
                 << ss.str() << std::endl;
    return false;
  }

  auto table_it = tables_.find(stmt.tableName);
  if (table_it == tables_.end()) {
    LOG(WARNING) << "Table not found: " << stmt.tableName << std::endl;
    return false;
  }
  auto &table = table_it->second;

  if (stmt.columns->size() != stmt.values->size()) {
    // TODO
    LOG(WARNING) << "Column count mismatch: " << stmt.columns->size() << " vs "
                 << stmt.values->size() << std::endl;
    return false;
  }

  CacheKey key;
  CacheEntry entry;
  key.table = stmt.tableName;
  key.primary_keys.resize(table.primary_keys_size);
  entry.values.resize(table.values_size);
  size_t primary_key_cnt = 0;

  for (size_t i = 0; i < stmt.columns->size(); i++) {
    auto column_it = table.columns_name_to_index.find((*stmt.columns)[i]);
    if (column_it == table.columns_name_to_index.end()) {
      LOG(WARNING) << "Column not found: " << (*stmt.columns)[i] << std::endl;
      return false;
    }
    auto column = table.columns[column_it->second];

    Value value;
    if (!ExprToValue((*stmt.values)[i], value)) {
      return false;
    }
    if (!ValueCast(value, column.type)) {
      return false;
    }

    if (column.is_primary_key) {
      primary_key_cnt++;
      key.primary_keys[column_it->second] = value;
    } else {
      entry.values[column_it->second - table.primary_keys_size] = value;
    }
  }

  if (primary_key_cnt != table.primary_keys_size) {
    LOG(WARNING) << "Primary key count mismatch: " << primary_key_cnt << " vs "
                 << table.primary_keys_size << std::endl;
    return false;
  }

  cache->Add(key, entry);
  UpdateQueryCache(key, nullptr, &entry, query_cache);
  return true;
}

bool TableCache::HandleUpdate(const hsql::UpdateStatement &stmt, Cache *cache,
                              QueryCache *query_cache) {
  // TODO: support other where clauses
  if (stmt.where->type == hsql::kExprOperator &&
      stmt.where->opType == hsql::kOpEquals &&
      stmt.where->expr->type == hsql::kExprColumnRef &&
      stmt.where->expr2->type == hsql::kExprLiteralInt &&
      stmt.table->type == hsql::kTableName) {
    const std::string table_name = stmt.table->name;
    auto table_it = tables_.find(table_name);
    if (table_it == tables_.end()) {
      LOG(WARNING) << "Table not found: " << table_name << std::endl;
      return false;
    }
    auto &table = table_it->second;

    auto column_it = table.columns_name_to_index.find(stmt.where->expr->name);
    if (column_it == table.columns_name_to_index.end()) {
      LOG(WARNING) << "Column not found: " << stmt.where->expr->name
                   << std::endl;
      return false;
    }
    auto column = table.columns[column_it->second];

    CacheKey key;
    key.table = table_name;
    key.primary_keys.resize(table.primary_keys_size);
    Value value;
    if (!ExprToValue(stmt.where->expr2, value)) {
      return false;
    }
    if (!ValueCast(value, column.type)) {
      return false;
    }
    key.primary_keys[column_it->second] = value;

    // TODO: cache add a replace []() function
    CacheEntry old_entry;
    if (!cache->Get(key, old_entry)) {
      // std::stringstream ss;
      // hsql::printStatementInfo(ss, &stmt);
      // LOG(WARNING) << "Key not found: " << ss.str() << std::endl;
      return false;
    }
    auto new_entry = old_entry;
    for (const auto update_clause : *stmt.updates) {
      auto column_it = table.columns_name_to_index.find(update_clause->column);
      if (column_it == table.columns_name_to_index.end()) {
        LOG(WARNING) << "Column not found: " << update_clause->column
                     << std::endl;
        return false;
      }
      auto column = table.columns[column_it->second];

      if (column.is_primary_key) {
        // TODO
        LOG(WARNING) << "Primary key update is not supported" << std::endl;
        return false;
      }

      switch (update_clause->value->type) {
        case hsql::kExprLiteralString:
        case hsql::kExprLiteralInt: {
          Value value;
          if (!ExprToValue(update_clause->value, value)) {
            return false;
          }
          if (!ValueCast(value, column.type)) {
            return false;
          }
          new_entry.values[column_it->second - table.primary_keys_size] = value;
          break;
        }
        case hsql::kExprOperator: {
          if (update_clause->value->expr->type == hsql::kExprColumnRef &&
              !strcmp(update_clause->value->expr->name,
                      update_clause->column) &&
              update_clause->value->expr2->type == hsql::kExprLiteralInt &&
              update_clause->value->opType == hsql::kOpPlus) {
            Value rhs;
            if (!ExprToValue(update_clause->value->expr2, rhs)) {
              return false;
            }
            if (!ValueCast(rhs, column.type)) {
              return false;
            }
            auto &lhs =
                new_entry.values[column_it->second - table.primary_keys_size];
            if (lhs.has_value()) lhs = lhs.value() + rhs;
          } else {
            std::stringstream ss;
            hsql::printExpression(ss, update_clause->value, 0);
            LOG(WARNING) << "Unsupported update type: "
                         << update_clause->value->type << std::endl
                         << ss.str() << std::endl;
            return false;
          }
          break;
        }
        default:
          LOG(WARNING) << "Unsupported update type: "
                       << update_clause->value->type << std::endl;
          return false;
      }
    }
    cache->Replace(key, new_entry);
    UpdateQueryCache(key, &old_entry, &new_entry, query_cache);
    return true;
  }

  // TODO: invalidation

  std::stringstream ss;
  hsql::printExpression(ss, stmt.where, 0);
  LOG(WARNING) << "Unsupported where clause:" << std::endl
               << ss.str() << std::endl;

  return false;
}

std::optional<bool> TableCache::WhereMatch(const CacheKey &key,
                                           const CacheEntry &entry,
                                           hsql::Expr *where) {
  // TODO: other expr
  if (where->type == hsql::kExprOperator && where->opType == hsql::kOpEquals &&
      where->expr->type == hsql::kExprColumnRef &&
      where->expr2->type == hsql::kExprLiteralInt) {
    auto table_it = tables_.find(key.table);
    if (table_it == tables_.end()) {
      LOG(WARNING) << "WhereMatch: Table not found: " << key.table << std::endl;
      return false;
    }
    auto &table = table_it->second;

    auto column_it = table.columns_name_to_index.find(where->expr->name);
    if (column_it == table.columns_name_to_index.end()) {
      LOG(ERROR) << "WhereMatch: Column not found: " << where->expr->name
                 << std::endl;
    }
    auto column = table.columns[column_it->second];

    std::optional<Value> value;
    if (column.is_primary_key) {
      value = key.primary_keys[column_it->second];
    } else {
      value = entry.values[column_it->second - table.primary_keys_size];
    }
    if (!value.has_value()) return std::nullopt;

    Value expected_value;
    if (!ExprToValue(where->expr2, expected_value)) {
      LOG(ERROR) << "WhereMatch: ExprToValue failed" << std::endl;
    }

    return value.value() == expected_value;
  } else if (where->type == hsql::kExprOperator &&
             where->opType == hsql::kOpBetween &&
             where->expr->type == hsql::kExprColumnRef &&
             (*where->exprList)[0]->type == hsql::kExprLiteralInt &&
             (*where->exprList)[1]->type == hsql::kExprLiteralInt) {
    auto table_it = tables_.find(key.table);
    if (table_it == tables_.end()) {
      LOG(WARNING) << "WhereMatch: Table not found: " << key.table << std::endl;
      return false;
    }
    auto &table = table_it->second;

    auto column_it = table.columns_name_to_index.find(where->expr->name);
    if (column_it == table.columns_name_to_index.end()) {
      LOG(ERROR) << "WhereMatch: Column not found: " << where->expr->name
                 << std::endl;
    }
    auto column = table.columns[column_it->second];

    std::optional<Value> value;
    if (column.is_primary_key) {
      value = key.primary_keys[column_it->second];
    } else {
      value = entry.values[column_it->second - table.primary_keys_size];
    }
    if (!value.has_value()) return std::nullopt;

    Value lower_bound;
    if (!ExprToValue((*where->exprList)[0], lower_bound)) {
      LOG(ERROR) << "WhereMatch: ExprToValue failed" << std::endl;
    }
    if (!ValueCast(lower_bound, column.type)) {
      LOG(ERROR) << "WhereMatch: ValueCast failed" << std::endl;
    }
    if (!(value.value() <= lower_bound)) return false;

    Value upper_bound;
    if (!ExprToValue((*where->exprList)[1], upper_bound)) {
      LOG(ERROR) << "WhereMatch: ExprToValue failed" << std::endl;
    }
    if (!ValueCast(upper_bound, column.type)) {
      LOG(ERROR) << "WhereMatch: ValueCast failed" << std::endl;
    }
    return value.value() >= upper_bound;
  }

  LOG(WARNING) << "Unsupported where clause:" << std::endl;
  return std::nullopt;
}

void TableCache::UpdateQueryCache(const CacheKey &key,
                                  const CacheEntry *old_entry,
                                  const CacheEntry *new_entry,
                                  QueryCache *query_cache) {
  if (!query_cache) return;  // normal mode

  auto query_cache_table_it = query_cache->query_cache_.find(key.table);
  if (query_cache_table_it == query_cache->query_cache_.end()) return;

  for (auto &table_query_cache_entry : query_cache_table_it->second) {
    std::optional<bool> old_entry_match =
        old_entry
            ? WhereMatch(key, *old_entry, table_query_cache_entry.second.where)
            : false;
    std::optional<bool> new_entry_match =
        new_entry
            ? WhereMatch(key, *new_entry, table_query_cache_entry.second.where)
            : false;

    if (!old_entry_match.has_value() || !new_entry_match.has_value()) {
      // TODO: invalidate
    }

    for (auto &result_table_entry :
         *table_query_cache_entry.second.result_table) {
      // TODO: support expr other than plain select
      auto column_name =
          result_table_entry.second.select->selectList->at(0)->name;
      auto column_it =
          tables_[key.table].columns_name_to_index.find(column_name);
      if (column_it == tables_[key.table].columns_name_to_index.end()) {
        LOG(ERROR) << "Column not found: " << column_name << std::endl;
      }
      auto column = tables_[key.table].columns[column_it->second];

      std::optional<Value> old_value;
      if (old_entry_match.value()) {
        if (column.is_primary_key) {
          old_value = key.primary_keys[column_it->second];
        } else {
          old_value = old_entry->values[column_it->second -
                                        tables_[key.table].primary_keys_size];
        }
      }
      if (!old_value.has_value()) continue;

      std::optional<Value> new_value;
      if (new_entry_match.value()) {
        if (column.is_primary_key) {
          new_value = key.primary_keys[column_it->second];
        } else {
          new_value = new_entry->values[column_it->second -
                                        tables_[key.table].primary_keys_size];
        }
      }

      // TODO: support limit
      auto &result = result_table_entry.second.result;
      if (old_entry_match.value()) {
        if (new_entry_match.value()) {
          // both match

          // remove old value
          result.rows.erase(std::remove(result.rows.begin(), result.rows.end(),
                                        std::vector<Value>{old_value.value()}),
                            result.rows.end());
          // add new value
          if (result_table_entry.second.select->order) {
            if (result_table_entry.second.select->order->size() == 1 &&
                (*result_table_entry.second.select->order)[0]->expr->type ==
                    hsql::kExprColumnRef &&
                (*result_table_entry.second.select->order)[0]->expr->name ==
                    column_name) {
              size_t i = 0;
              if ((*result_table_entry.second.select->order)[0]->type ==
                  hsql::kOrderAsc) {
                for (; i < result.rows.size(); i++) {
                  if (result.rows[i][0] > new_value.value()) break;
                }
              } else {
                for (; i < result.rows.size(); i++) {
                  if (result.rows[i][0] < new_value.value()) break;
                }
              }
              result.rows.insert(result.rows.begin() + i, {new_value.value()});
            } else {
              // TODO
            }
          } else {
            result.rows.push_back({new_value.value()});
          }
        } else {
          // old match, new not match
          // TODO
        }
      } else if (new_entry_match.value()) {
        // old not match, new match
        // TODO
      } else {
        // both not match, do nothing
        // TODO
      }
    }
  }
}