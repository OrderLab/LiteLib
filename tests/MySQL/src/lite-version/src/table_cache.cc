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
  schema.columns_index_to_name = {{0, "id"}, {1, "k"}, {2, "c"}, {3, "pad"}};

  tables_["sbtest1"] = schema;
  tables_["sbtest2"] = schema;
}

void TableCache::Dump(Cache *cache) {
  cache->ConstVisitAll([&](const auto &key, const auto &entry) {
    LOG(INFO) << "Key: " << key.table << std::endl;
    for (size_t i = 0; i < key.primary_keys.size(); ++i) {
      LOG(INFO) << "Primary key " << i << ": " << ValueToString(key.primary_keys[i]) << std::endl;
    }
    for (size_t i = 0; i < entry.values.size(); ++i) {
      if (entry.values[i].has_value()) {
        LOG(INFO) << "Value " << i << ": " << ValueToString(entry.values[i].value())
                  << std::endl;
      } else {
        LOG(INFO) << "Value " << i << ": " << "UNKNOWN" << std::endl;
      }
    }
  });
}

bool TableCache::HandleInsert(const hsql::InsertStatement &stmt, Cache *cache,
                              QueryCache *query_cache,
                              bool update_query_cache) {
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

  cache->Add(key, entry, false, false);
  UpdateQueryCache(key, nullptr, &entry, query_cache, update_query_cache);
  return true;
}

bool TableCache::HandleDelete(const hsql::DeleteStatement &stmt, Cache *cache,
                              QueryCache *query_cache,
                              bool update_query_cache) {
  auto table_it = tables_.find(stmt.tableName);
  if (table_it == tables_.end()) {
    LOG(WARNING) << "Table not found: " << stmt.tableName << std::endl;
    return false;
  }
  auto &table = table_it->second;

  // TODO: support other queries
  if (table.primary_keys_size == 1 && stmt.expr->type == hsql::kExprOperator &&
      stmt.expr->opType == hsql::kOpEquals &&
      stmt.expr->expr->type == hsql::kExprColumnRef &&
      stmt.expr->expr2->type == hsql::kExprLiteralInt) {
    auto column_it = table.columns_name_to_index.find(stmt.expr->expr->name);
    if (column_it == table.columns_name_to_index.end()) {
      LOG(WARNING) << "Column not found: " << stmt.expr->expr->name
                   << std::endl;
      return false;
    }
    if (column_it->second >= table.primary_keys_size) {
      // TODO: iterate through all entries
      return false;
    }
    auto column = table.columns[column_it->second];

    CacheKey key;
    key.table = stmt.tableName;
    key.primary_keys.resize(table.primary_keys_size);
    Value value;
    if (!ExprToValue(stmt.expr->expr2, value)) {
      return false;
    }
    if (!ValueCast(value, column.type)) {
      return false;
    }
    key.primary_keys[column_it->second] = value;

    CacheEntry entry;
    if (!cache->Get(key, entry)) {
      // std::stringstream ss;
      // hsql::printStatementInfo(ss, &stmt);
      // LOG(WARNING) << "Key not found: " << ss.str() << std::endl;
      return false;
    }
    cache->Delete(key);
    UpdateQueryCache(key, &entry, nullptr, query_cache, update_query_cache);
    return true;
  }

  return false;
}

bool TableCache::HandleUpdate(const hsql::UpdateStatement &stmt, Cache *cache,
                              QueryCache *query_cache,
                              bool update_query_cache) {
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
    cache->Replace(key, new_entry, false, false);
    UpdateQueryCache(key, &old_entry, &new_entry, query_cache,
                     update_query_cache);
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
                                           const hsql::Expr *where) {
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
    if (!(lower_bound <= value.value())) return false;

    Value upper_bound;
    if (!ExprToValue((*where->exprList)[1], upper_bound)) {
      LOG(ERROR) << "WhereMatch: ExprToValue failed" << std::endl;
    }
    if (!ValueCast(upper_bound, column.type)) {
      LOG(ERROR) << "WhereMatch: ValueCast failed" << std::endl;
    }
    return value.value() <= upper_bound;
  }

  std::stringstream ss;
  hsql::printExpression(ss, const_cast<hsql::Expr *>(where), 0);
  LOG(WARNING) << "Unsupported where clause:" << std::endl
               << ss.str() << std::endl;
  return std::nullopt;
}

void TableCache::GetLowerBoundAndUpperBound(const hsql::Expr *where,
                                            Value &lower_bound,
                                            Value &upper_bound) {
  if (where->opType == hsql::kOpBetween) {
    if (!ExprToValue((*where->exprList)[0], lower_bound)) {
      LOG(ERROR) << "WhereMatch: ExprToValue failed" << std::endl;
    }
    if (!ValueCast(lower_bound, kLL)) {
      LOG(ERROR) << "WhereMatch: ValueCast failed" << std::endl;
    }

    if (!ExprToValue((*where->exprList)[1], upper_bound)) {
      LOG(ERROR) << "WhereMatch: ExprToValue failed" << std::endl;
    }
    if (!ValueCast(upper_bound, kLL)) {
      LOG(ERROR) << "WhereMatch: ValueCast failed" << std::endl;
    }
  } else if (where->opType == hsql::kOpEquals) {
    if (!ExprToValue(where->expr2, lower_bound)) {
      LOG(ERROR) << "WhereMatch: ExprToValue failed" << std::endl;
    }
    if (!ValueCast(lower_bound, kLL)) {
      LOG(ERROR) << "WhereMatch: ValueCast failed" << std::endl;
    }
    upper_bound = lower_bound;
  }
}

void TableCache::UpdateQueryCache(const CacheKey &key,
                                  const CacheEntry *old_entry,
                                  const CacheEntry *new_entry,
                                  QueryCache *query_cache,
                                  bool update_query_cache) {
  if (!update_query_cache) {
    // all where clauses of the same template are the same in terms of known or
    // unknown template one: range/point select on an index
    //   as we only store rows whose primary key is known in the cache, so we
    //   can directly use the index to get related query and results
    query_cache->table_query_caches_.visit(
        key.table, [&](auto &table_query_cache_it) {
          auto &[_, table_query_cache] = table_query_cache_it;
          table_query_cache.column_range_indices.visit(
              tables_[key.table].columns_index_to_name[0],
              [&](auto &column_range_index_it) {
                auto &[_, column_range_index] = column_range_index_it;
                // find all related query and results matched old_entry and
                // remove them
                auto related_query_and_results =
                    column_range_index->Query(std::get<kLL>(key.primary_keys[0]));
                std::unordered_set<std::string> where_strs;
                std::unordered_map<std::string, std::pair<Value, Value>>
                    where_bounds;
                for (const auto &related_query_and_result :
                     related_query_and_results) {
                  where_strs.insert(related_query_and_result->GetWhereClause());
                  auto where_bound_it = where_bounds.find(
                      related_query_and_result->GetWhereClause());
                  if (where_bound_it == where_bounds.end()) {
                    Value lower_bound, upper_bound;
                    std::shared_lock query_and_result_lock(
                        *(related_query_and_result->mutex_ptr));
                    const auto where =
                        related_query_and_result->GetSelectStatement()
                            ->whereClause;
                    GetLowerBoundAndUpperBound(where, lower_bound, upper_bound);
                    auto [it, _] = where_bounds.insert(std::make_pair(
                        related_query_and_result->GetWhereClause(),
                        std::make_pair(lower_bound, upper_bound)));
                    where_bound_it = it;
                  }
                  column_range_index->Delete(
                      related_query_and_result,
                      std::get<kLL>(where_bound_it->second.first),
                      std::get<kLL>(where_bound_it->second.second));
                }
                for (const auto &where_str : where_strs) {
                  table_query_cache.where_query_caches.erase(where_str);
                }
              });
        });
    // TODO: template two: others, do the things in the following code. But in
    // Sysbench, there are no other kinds of where clauses, so we skip it
    return;
  }
  query_cache->table_query_caches_.visit(
      key.table, [&](auto &table_query_cache_it) {
        auto &[table, table_query_cache] = table_query_cache_it;
        table_query_cache.where_query_caches.erase_if(
            [&](auto &where_query_cache_it) {
              auto &[_, where_query_cache] = where_query_cache_it;
              std::optional<bool> old_entry_match, new_entry_match;
              where_query_cache.query_and_results.cvisit_while(
                  [&](const auto &query_and_result_it) {
                    auto &[query, query_and_result] = query_and_result_it;
                    std::shared_lock query_and_result_lock(
                        *(query_and_result->mutex_ptr));
                    const auto where =
                        query_and_result->GetSelectStatement()->whereClause;
                    old_entry_match =
                        old_entry ? WhereMatch(key, *old_entry, where) : false;
                    new_entry_match =
                        new_entry ? WhereMatch(key, *new_entry, where) : false;
                    return false;
                  });

              if (!old_entry_match.has_value() ||
                  !new_entry_match.has_value()) {
                goto remove_where_clause;
              }

              if (!update_query_cache) {
                if (!old_entry_match.value()) {
                  return false;
                } else {
                  goto remove_where_clause;
                }
              }

              where_query_cache.query_and_results.erase_if(
                  [&](auto &query_and_result_it) {
                    auto &[_, query_and_result] = query_and_result_it;
                    // TODO: diff entry in select list are diff types
                    if (query_and_result->GetSelectStatement()
                            ->selectList->at(0)
                            ->type == hsql::kExprColumnRef) {
                      std::vector<size_t> index;
                      for (auto select_id = 0;
                           select_id < query_and_result->GetSelectStatement()
                                           ->selectList->size();
                           ++select_id) {
                        auto column_name =
                            query_and_result->GetSelectStatement()
                                ->selectList->at(select_id)
                                ->name;
                        auto column_it =
                            tables_[key.table].columns_name_to_index.find(
                                column_name);
                        if (column_it ==
                            tables_[key.table].columns_name_to_index.end()) {
                          LOG(ERROR) << "Column not found: " << column_name
                                     << std::endl;
                        }
                        index.push_back(column_it->second);
                      }

                      bool invalidate = false;
                      std::vector<Value> old_row;
                      if (old_entry_match.value()) {
                        for (auto i = 0; i < index.size(); i++) {
                          std::optional<Value> old_value;
                          if (index[i] < tables_[key.table].primary_keys_size) {
                            old_value = key.primary_keys[index[i]];
                          } else {
                            old_value =
                                old_entry
                                    ->values[index[i] - tables_[key.table]
                                                            .primary_keys_size];
                          }
                          if (!old_value.has_value()) {
                            invalidate = true;
                            break;
                          }
                          old_row.push_back(old_value.value());
                        }
                      }

                      std::vector<Value> new_row;
                      if (new_entry_match.value()) {
                        for (auto i = 0; i < index.size(); i++) {
                          std::optional<Value> value;
                          if (index[i] < tables_[key.table].primary_keys_size) {
                            value = key.primary_keys[index[i]];
                          } else {
                            value =
                                new_entry
                                    ->values[index[i] - tables_[key.table]
                                                            .primary_keys_size];
                          }
                          if (!value.has_value()) {
                            invalidate = true;
                            break;
                          }
                          new_row.push_back(value.value());
                        }
                      }

                      if (invalidate) {
                        // TODO: support unknown values
                        goto remove_query_and_result;
                      }

                      // TODO: support limit
                      auto &result = query_and_result->result;

                      std::unique_lock query_and_result_lock(
                          *(query_and_result->mutex_ptr));

                      // remove old value
                      if (old_entry_match.value()) {
                        auto pos = std::find(result.rows.begin(),
                                             result.rows.end(), old_row);
                        if (pos != result.rows.end()) {
                          result.rows.erase(pos);
                        } else {
                          LOG(WARNING) << "Old row not found" << std::endl;
                          goto remove_query_and_result;
                        }
                      }

                      // add new value
                      if (new_entry_match.value()) {
                        if (query_and_result->GetSelectStatement()->order) {
                          if (query_and_result->GetSelectStatement()
                                      ->order->size() == 1 &&
                              (*query_and_result->GetSelectStatement()
                                    ->order)[0]
                                      ->expr->type == hsql::kExprColumnRef) {
                            auto column_it =
                                tables_[key.table].columns_name_to_index.find(
                                    (*query_and_result->GetSelectStatement()
                                          ->order)[0]
                                        ->expr->name);
                            if (column_it == tables_[key.table]
                                                 .columns_name_to_index.end()) {
                              LOG(ERROR)
                                  << "Column not found: "
                                  << (*query_and_result->GetSelectStatement()
                                           ->order)[0]
                                         ->expr->name
                                  << std::endl;
                              goto remove_query_and_result;
                            }

                            size_t index_in_row =
                                std::find(index.begin(), index.end(),
                                          column_it->second) -
                                index.begin();

                            size_t i = 0;
                            if ((*query_and_result->GetSelectStatement()
                                      ->order)[0]
                                    ->type == hsql::kOrderAsc) {
                              for (; i < result.rows.size(); i++) {
                                if (result.rows[i][index_in_row] >
                                    new_row[index_in_row])
                                  break;
                              }
                            } else {
                              for (; i < result.rows.size(); i++) {
                                if (result.rows[i][index_in_row] <
                                    new_row[index_in_row])
                                  break;
                              }
                            }
                            result.rows.insert(result.rows.begin() + i,
                                               new_row);

                            if (query_and_result->GetSelectStatement()
                                    ->selectDistinct) {
                              result.rows.erase(std::unique(result.rows.begin(),
                                                            result.rows.end()),
                                                result.rows.end());
                            }
                          } else {
                            // TODO: support other orders
                            goto remove_query_and_result;
                          }
                        } else {
                          result.rows.push_back(new_row);
                        }
                      }
                    } else if (query_and_result->GetSelectStatement()
                                       ->selectList->at(0)
                                       ->type == hsql::kExprFunctionRef &&
                               !strcmp(query_and_result->GetSelectStatement()
                                           ->selectList->at(0)
                                           ->name,
                                       "SUM")) {
                      std::vector<size_t> index;
                      for (auto select_id = 0;
                           select_id < query_and_result->GetSelectStatement()
                                           ->selectList->size();
                           ++select_id) {
                        auto column_name =
                            query_and_result->GetSelectStatement()
                                ->selectList->at(select_id)
                                ->exprList->at(0)
                                ->name;
                        auto column_it =
                            tables_[key.table].columns_name_to_index.find(
                                column_name);
                        if (column_it ==
                            tables_[key.table].columns_name_to_index.end()) {
                          LOG(ERROR) << "Column not found: " << column_name
                                     << std::endl;
                        }
                        index.push_back(column_it->second);
                      }

                      bool invalidate = false;
                      std::vector<Value> old_row;
                      if (old_entry_match.value()) {
                        for (auto i = 0; i < index.size(); i++) {
                          std::optional<Value> old_value;
                          if (index[i] < tables_[key.table].primary_keys_size) {
                            old_value = key.primary_keys[index[i]];
                          } else {
                            old_value =
                                old_entry
                                    ->values[index[i] - tables_[key.table]
                                                            .primary_keys_size];
                          }
                          if (!old_value.has_value()) {
                            invalidate = true;
                            break;
                          }
                          old_row.push_back(old_value.value());
                        }
                      }

                      std::vector<Value> new_row;
                      if (new_entry_match.value()) {
                        for (auto i = 0; i < index.size(); i++) {
                          std::optional<Value> value;
                          if (index[i] < tables_[key.table].primary_keys_size) {
                            value = key.primary_keys[index[i]];
                          } else {
                            value =
                                new_entry
                                    ->values[index[i] - tables_[key.table]
                                                            .primary_keys_size];
                          }
                          if (!value.has_value()) {
                            invalidate = true;
                            break;
                          }
                          new_row.push_back(value.value());
                        }
                      }

                      if (invalidate) {
                        goto remove_query_and_result;
                      }

                      auto &result = query_and_result->result;

                      std::unique_lock query_and_result_lock(
                          *(query_and_result->mutex_ptr));

                      if (old_entry_match.value()) {
                        for (size_t i = 0; i < old_row.size(); i++) {
                          result.rows[0][i] -= old_row[i];
                        }
                      }

                      if (new_entry_match.value()) {
                        for (size_t i = 0; i < new_row.size(); i++) {
                          result.rows[0][i] += new_row[i];
                        }
                      }
                    } else {
                      // std::unique_lock query_and_result_lock(
                      //     *query_and_result.mutex_ptr);
                      LOG(WARNING) << "Unsupported select type: "
                                   << query_and_result->GetSelectStatement()
                                          ->selectList->at(0)
                                          ->type
                                   << std::endl;
                      goto remove_query_and_result;
                    }
                    return false;

                  remove_query_and_result:
                    auto column_name = query_and_result->GetSelectStatement()
                                           ->whereClause->expr->name;
                    table_query_cache.column_range_indices.visit(
                        column_name, [&](auto &column_range_index_it) {
                          auto &[_, column_range_index] = column_range_index_it;
                          column_range_index->Delete(query_and_result);
                        });
                    return true;
                  });

              return where_query_cache.query_and_results.empty();

            remove_where_clause:

              Value lower_bound, upper_bound;
              std::string column_name;

              where_query_cache.query_and_results.cvisit_while(
                  [&](const auto &query_and_result_it) {
                    auto &[query, query_and_result] = query_and_result_it;
                    std::shared_lock query_and_result_lock(*(query_and_result->mutex_ptr));
                    const auto where = query_and_result->GetSelectStatement()->whereClause;
                    GetLowerBoundAndUpperBound(where, lower_bound, upper_bound);
                    column_name = where->expr->name;
                    return false;
                  });

              // if (column.type != kLL) {
              //   // TODO: support other types
              //   LOG(ERROR) << "Unsupported type for query index: "
              //              << column.type << std::endl;
              //   return true;
              // }

              where_query_cache.query_and_results.erase_if(
                  [&](auto &query_and_result_it) {
                    auto &[_, query_and_result] = query_and_result_it;

                    table_query_cache.column_range_indices.visit(
                        column_name, [&](auto &column_range_index_it) {
                          auto &[_, column_range_index] = column_range_index_it;
                          column_range_index->Delete(
                              query_and_result,
                              std::get<kLL>(lower_bound),
                              std::get<kLL>(upper_bound));
                        });

                    return true;
                  });

              return true;
            });
      });
}

std::optional<Packet> TableCache::ServePointSelect(
    const hsql::SelectStatement &stmt, Cache *cache) {
  auto table_it = tables_.find(stmt.fromTable->name);
  if (table_it == tables_.end()) {
    LOG(WARNING) << "Table not found: " << stmt.fromTable->name << std::endl;
    return std::nullopt;
  }
  auto &table = table_it->second;

  // TDDO: support multiple primary keys
  if (stmt.whereClause->type != hsql::kExprOperator ||
      stmt.whereClause->opType != hsql::kOpEquals ||
      stmt.whereClause->expr->type != hsql::kExprColumnRef ||
      stmt.whereClause->expr2->type != hsql::kExprLiteralInt) {
    return std::nullopt;
  }

  CacheKey key;
  key.table = stmt.fromTable->name;
  key.primary_keys.resize(table.primary_keys_size);
  auto column_it =
      table.columns_name_to_index.find(stmt.whereClause->expr->name);
  if (column_it == table.columns_name_to_index.end()) {
    LOG(WARNING) << "Column not found: " << stmt.whereClause->expr->name
                 << std::endl;
    return std::nullopt;
  }
  auto column = table.columns[column_it->second];
  Value value;
  if (!ExprToValue(stmt.whereClause->expr2, value)) {
    return std::nullopt;
  }
  if (!ValueCast(value, column.type)) {
    return std::nullopt;
  }
  key.primary_keys[column_it->second] = value;

  CacheEntry entry;
  if (!cache->Get(key, entry)) {
    return std::nullopt;
  }

  if (stmt.selectList->size() != 1) {
    LOG(WARNING) << "Unsupported select list size: " << stmt.selectList->size()
                 << std::endl;
    return std::nullopt;
  }
  auto selected_value =
      entry.values[table.columns_name_to_index[stmt.selectList->at(0)->name] -
                   table.primary_keys_size];
  if (!selected_value.has_value()) {
    return std::nullopt;
  }
  std::string value_string = ValueToString(selected_value.value());

  // TODO: parse table schema
  Packet packet;
  packet.buffer = std::make_shared<std::vector<uint8_t>>(std::vector<uint8_t>{
      0x01, 0x00, 0x00, 0x01, 0x01, 0x2c, 0x00, 0x00, 0x02, 0x03, 0x64,
      0x65, 0x66, 0x06, 0x73, 0x62, 0x74, 0x65, 0x73, 0x74, 0x07, 0x73,
      0x62, 0x74, 0x65, 0x73, 0x74, 0x32, 0x07, 0x73, 0x62, 0x74, 0x65,
      0x73, 0x74, 0x32, 0x01, 0x63, 0x01, 0x63, 0x0c, 0x08, 0x00, 0x78,
      0x00, 0x00, 0x00, 0xfe, 0x01, 0x00, 0x00, 0x00, 0x00, 0x05, 0x00,
      0x00, 0x03, 0xfe, 0x00, 0x00, 0x03, 0x00,
  });
  uint32_t packet_length = value_string.size() + 1;
  packet.buffer->insert(packet.buffer->end(), (uint8_t *)&packet_length,
                        (uint8_t *)&packet_length + 3);
  packet.buffer->push_back(0x4);
  packet_length--;
  packet.buffer->insert(packet.buffer->end(), (uint8_t *)&packet_length,
                        (uint8_t *)&packet_length + 1);
  packet.buffer->insert(packet.buffer->end(), value_string.begin(),
                        value_string.end());
  static const uint8_t kSuffix[] = {0x05, 0x00, 0x00, 0x05, 0xfe,
                                    0x00, 0x00, 0x03, 0x00};
  packet.buffer->insert(packet.buffer->end(), kSuffix,
                        kSuffix + sizeof(kSuffix));
  return packet;
}
