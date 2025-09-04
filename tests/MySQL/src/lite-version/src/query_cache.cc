#include "query_cache.hpp"

#include <fcntl.h>
#include <hsql/SQLParser.h>
#include <hsql/util/sqlhelper.h>
#include <sys/mman.h>
#include <sys/stat.h>

#include <lite.hpp>

#include "service.hpp"

#define RUST_CLIENT 0
// if it's sysbench client, set to 0

Result Result::Deserialize(std::vector<uint8_t> &buffer,
                           const hsql::SelectStatement *stmt) {
  Result result;

  const uint8_t *begin = buffer.data();
  uint8_t column_count = begin[4];
  for (uint8_t i = 0; i < 1 + RUST_CLIENT + column_count; i++) {
    uint32_t payload_length = begin[0] | begin[1] << 8 | begin[2] << 16;
    result.prefix_packets.insert(result.prefix_packets.end(), begin,
                                 begin + 4 + payload_length);
    begin += 4 + payload_length;
  }

  // TODO: actually use the query AST to get the format
  auto type =
      (*stmt->selectList)[0]->type == hsql::kExprColumnRef ? kVARCHAR : kLL;
  while (true) {
    if (begin[4] == 0xfe) break;  // EOF packet

    Row row;
    begin += 4;
    for (uint8_t i = 0; i < column_count; i++) {
      Value value = FetchValue(begin, kVARCHAR);
      ValueCast(value, column_count > 1
                           ? (i == column_count - 1 ? kVARCHAR : kLL)
                           : type);
      row.push_back(value);
    }

    result.rows.push_back(row);
  }

  result.suffix_packets.insert(result.suffix_packets.end(),
                               begin + 5,
                               (const uint8_t *)buffer.data() + buffer.size());

  return result;
}

std::shared_ptr<std::vector<uint8_t>> Result::Serialize() {
  auto buffer = std::make_shared<std::vector<uint8_t>>();
  buffer->insert(buffer->end(), prefix_packets.begin(), prefix_packets.end());

  uint8_t packet_number = (*buffer)[4] + 2 + RUST_CLIENT;  // column_count + 2 + RUST_CLIENT
  for (const auto &row : rows) {
    std::vector<uint8_t> values_buffer;

    for (size_t i = 0; i < row.size(); ++i) {
      Value value = row[i];
      ValueCast(value, kVARCHAR);
      auto value_buffer = ValueToNetworkBuffer(value);
      values_buffer.insert(values_buffer.end(), value_buffer.begin(),
                           value_buffer.end());
    }

    uint32_t row_packet_length = values_buffer.size();
    // packet length
    buffer->insert(buffer->end(), int2pointer(row_packet_length),
                   int2pointer(row_packet_length) + 3);
    // packet number
    buffer->push_back(packet_number++);
    // values
    buffer->insert(buffer->end(), values_buffer.begin(), values_buffer.end());
  }

  // EOF
  // length
  buffer->push_back(suffix_packets.size() + 1 + RUST_CLIENT);
  buffer->push_back(0x0);
  buffer->push_back(0x0);
  // packet number
  buffer->push_back(packet_number);
  // response code
  buffer->push_back(0xfe);
  // content
  buffer->insert(buffer->end(), suffix_packets.begin(), suffix_packets.end());

  return buffer;
}

QueryCache::QueryCache(MySQL &mysql) : mysql_(mysql) { ConnectToFull(); }

QueryCache::~QueryCache() { DisconnectFromFull(); }

int QueryCache::GetSizeAndDump(bool dump) {
  int cnt = 0;
  table_query_caches_.cvisit_all([&](const auto &table_query_cache_it) {
    auto &[table, table_query_cache] = table_query_cache_it;
    table_query_cache.where_query_caches.cvisit_all(
        [&](const auto &where_query_cache_it) {
          auto &[where, where_query_cache] = where_query_cache_it;
          cnt += where_query_cache.query_and_results.size();
          if (dump) {
            where_query_cache.query_and_results.cvisit_all(
                [&](const auto &query_and_result_it) {
                  auto &[query, query_and_result] = query_and_result_it;
                  LOG(INFO) << "Query: " << query << std::endl;
                  // auto result = query_and_result->result.Serialize();
                  // for (size_t i = 0; i < result->size(); ++i) {
                  //   std::cerr << result->at(i);
                  //   std::cerr << std::endl;
                  // }
                });
          }
        });
  });
  return cnt;
}

#define FULL_TO_LITE_FIFO "/tmp/mysql_full_to_lite"
#define LITE_TO_FULL_FIFO "/tmp/mysql_lite_to_full"

void QueryCache::ConnectToFull() {
  // -------------------------------------------- shared memory ---------------
  shm_fd_ = shm_open(query_cache_shm_name, O_RDONLY, 0666);
  if (shm_fd_ == -1) {
    PLOG(FATAL) << "Unable to open query cache shared memory";
  }

  // Get the size of the shared memory object
  struct stat sb;
  if (fstat(shm_fd_, &sb) == -1) {
    PLOG(FATAL) << "Unable to get the size of query cache shared memory";
  }
  shm_size_ = sb.st_size;
  LOG(INFO) << "Size of query cache shared memory: " << shm_size_ << " bytes"
            << std::endl;

  shm_ptr_ = static_cast<uchar *>(
      mmap(0, shm_size_, PROT_READ, MAP_SHARED, shm_fd_, 0));
  if (shm_ptr_ == MAP_FAILED) {
    PLOG(FATAL) << "Unable to map query cache shared memory";
  }
  LOG(INFO) << "Query cache shared memory mapped at address "
            << static_cast<void *>(shm_ptr_) << std::endl;

  shm_info_ = reinterpret_cast<AlignedShmInfo *>(shm_ptr_);
  LOG(INFO) << shm_info_->shm_info;

  shm_v_offset_ = shm_ptr_ - shm_info_->shm_info.vaddr;

  LOG(INFO) << "Offset of query cache shared memory: 0x" << std::hex
            << shm_v_offset_ << std::endl;

  // -------------------------------------------- FIFO ------------------------
  mkfifo(LITE_TO_FULL_FIFO, 0666);
  lite_to_full_fd_ = open(LITE_TO_FULL_FIFO, O_RDWR, 0);
  if (lite_to_full_fd_ == -1) PLOG(FATAL) << "open LITE_TO_FULL_FIFO";

  mkfifo(FULL_TO_LITE_FIFO, 0666);
  full_to_lite_fd_ = open(FULL_TO_LITE_FIFO, O_RDONLY | O_NONBLOCK, 0);
  if (full_to_lite_fd_ == -1) PLOG(FATAL) << "open FULL_TO_LITE_FIFO";

  struct event_config *ev_config;
  ev_config = event_config_new();
  event_config_set_flag(ev_config, EVENT_BASE_FLAG_NOLOCK);
  full_listener_base_ = event_base_new_with_config(ev_config);
  event_config_free(ev_config);
  event_set(&full_listener_event_, full_to_lite_fd_, EV_READ | EV_PERSIST,
            FullListenerHandler, this);
  event_base_set(full_listener_base_, &full_listener_event_);
  if (event_add(&full_listener_event_, 0) == -1)
    PLOG(FATAL) << "event_add full_listener_event";

  pthread_attr_t attr;
  pthread_attr_init(&attr);
  pthread_create(&full_listener_thread_, &attr, FullListenerThreadBody, this);
  pthread_setname_np(full_listener_thread_, "mysql_full_listener");
  pthread_attr_destroy(&attr);
}

void QueryCache::DisconnectFromFull() {
  // -------------------------------------------- shared memory ---------------
  if (munmap(shm_ptr_, shm_size_) == -1) {
    PLOG(FATAL) << "Unable to unmap query cache shared memory";
  }

  if (close(shm_fd_) == -1) {
    PLOG(FATAL) << "Unable to close query cache shared memory";
  }

  // -------------------------------------------- FIFO ------------------------
  event_del(&full_listener_event_);
  event_base_free(full_listener_base_);
  pthread_cancel(full_listener_thread_);
  pthread_join(full_listener_thread_, NULL);
  if (close(lite_to_full_fd_) == -1) PLOG(ERROR) << "close lite_to_full_fd";
  if (close(full_to_lite_fd_) == -1) PLOG(ERROR) << "close full_to_lite_fd";
}

void QueryCache::WaitForAllNotificationsFromFull() {
  // Wait until all workers have processed their notifications from the full MySQL server
  // This ensures that all query cache blocks have been processed before transitioning
  
  // Use the barrier mechanism to wait for all workers
  mysql_.WaitForAllWorkersBarrier();
  
  LOG(INFO) << "All workers have finished processing notifications from full MySQL server" << std::endl;
}

void QueryCache::EmergencyToNormalHook() { /* TODO: uncomment this */
  // ConnectToFull();
}

bool QueryCache::NormalToEmergencyHook(TableCache &table_cache, Cache *cache) {
  WaitForAllNotificationsFromFull();

  if (shm_info_->shm_info.queries_blocks == 0) {
    LOG(WARNING) << "No query cache block in shared memory" << std::endl;
  } else {
    auto query_block_ptr =
        shm_info_->shm_info.queries_blocks_with_vaddr_offset(shm_v_offset_);
    const auto query_block_linked_list_head = query_block_ptr;

    do {
      AddQueryCacheBlock(query_block_ptr, table_cache, cache);
    } while ((query_block_ptr = query_block_ptr->next_with_vaddr_offset(
                  shm_v_offset_)) != query_block_linked_list_head);
  }

  DisconnectFromFull();

  BuildRelationsBetweenQueryAndCachedRows();

  int cnt = GetSizeAndDump(true);
  LOG(INFO) << "Query cache size: " << cnt << std::endl;

  return true;
}

void QueryCache::AddQueryCacheBlock(
    Query_cache_block *query_cache_block_lite_ptr, TableCache &table_cache,
    Cache *cache) {
  std::vector<uint8_t> result;
  const auto query_ptr =
      query_cache_block_lite_ptr->query_with_vaddr_offset(shm_v_offset_);
  // LOG(INFO) << "Query: " << query_ptr->query_with_vaddr_offset(shm_v_offset_)
  //           << std::endl;
  auto result_block_ptr = query_ptr->result_with_vaddr_offset(shm_v_offset_);
  const auto result_block_linked_list_head = result_block_ptr;
  do {
    const auto result_ptr =
        result_block_ptr->result_with_vaddr_offset(shm_v_offset_);
    // LOG(INFO) << "  Result len: " << result_block_ptr->result_data_len()
    //           << std::endl;
    // LOG(INFO) << "  Result content: ";
    auto result_data = result_ptr->data_with_vaddr_offset(shm_v_offset_);
    result.insert(result.end(), result_data,
                  result_data + result_block_ptr->result_data_len());
    // for (size_t i = 0; i < result_block_ptr->result_data_len(); ++i) {
    //   result.push_back(result_data[i]);
    //   // std::cerr << result_ptr->data_with_vaddr_offset(shm_v_offset_)[i];
    // }
    // TODO: reduce the number of memcpy
  } while ((result_block_ptr = result_block_ptr->next_with_vaddr_offset(
                shm_v_offset_)) != result_block_linked_list_head);
  AddQueryAndResult((char *)query_ptr->query_with_vaddr_offset(shm_v_offset_),
                    result, table_cache, cache);
}

void QueryCache::AddQueryAndResult(std::string query,
                                   std::vector<uint8_t> &result,
                                   TableCache &table_cache, Cache *cache) {
  // LOG(INFO) << "AddQueryAndResult for query: " << query << std::endl;
  hsql::SQLParserResult parse_result;
  hsql::SQLParser::parse(query, &parse_result);
  if (!parse_result.isValid()) {
    LOG(ERROR) << "Query Cache Init: unable to parse query: " << query
               << std::endl
               << "\t" << parse_result.errorMsg() << " L"
               << parse_result.errorLine() << ':' << parse_result.errorColumn()
               << std::endl;
    return;
  }
  if (parse_result.size() != 1) {
    LOG(WARNING) << "Query Cache Init: multiple queries in one query string: "
                 << query << std::endl;
    return;
  }
  if (parse_result.getStatement(0)->type() !=
      hsql::StatementType::kStmtSelect) {
    LOG(WARNING) << "Query Cache Init: not a select query: " << query
                 << std::endl;
    return;
  }
  auto select_stmt =
      dynamic_cast<const hsql::SelectStatement *>(parse_result.getStatement(0));

  // TODO: what if the where statement is a nested select?
  std::stringstream where_stream;
  if (select_stmt->whereClause != nullptr)
    printExpression(where_stream, select_stmt->whereClause, 0);
  auto parsed_result = Result::Deserialize(result, select_stmt);

  // def here to enable goto
  size_t number_of_primary_keys_in_select_stmt = 0;
  std::vector<size_t> index;
  decltype(table_cache.tables_)::iterator table_it;
  TableSchema *table;
  CacheKey where_key;
  CacheEntry where_entry;

  // insert to table cache
  if (select_stmt->fromTable->type != hsql::kTableName)
    goto insert_to_query_cache;
  table_it = table_cache.tables_.find(select_stmt->fromTable->getName());
  if (table_it == table_cache.tables_.end()) {
    LOG(WARNING) << "Table not found: " << select_stmt->fromTable->getName()
                 << std::endl;
    goto insert_to_query_cache;
  }
  table = &table_it->second;
  for (const auto &select : *(select_stmt->selectList)) {
    if (select->type == hsql::kExprColumnRef) {
      auto column_it = table->columns_name_to_index.find(select->name);
      if (column_it == table->columns_name_to_index.end()) {
        LOG(WARNING) << "Column not found: " << select->name << std::endl;
        goto insert_to_query_cache;
      }
      index.push_back(column_it->second);
      number_of_primary_keys_in_select_stmt +=
          column_it->second < table->primary_keys_size;
    } else {
      goto insert_to_query_cache;
    }
  }
  where_key.table = select_stmt->fromTable->getName();
  where_key.primary_keys.resize(table->primary_keys_size);
  where_entry.values.resize(table->values_size);
  // TODO: support multiple keys point selection
  if (select_stmt->whereClause->type == hsql::kExprOperator &&
      select_stmt->whereClause->opType == hsql::kOpEquals &&
      select_stmt->whereClause->expr->type == hsql::kExprColumnRef) {
    auto column_it =
        table->columns_name_to_index.find(select_stmt->whereClause->expr->name);
    if (column_it == table->columns_name_to_index.end()) {
      LOG(WARNING) << "Column not found: "
                   << select_stmt->whereClause->expr->name << std::endl;
      goto insert_to_query_cache;
    }
    if (column_it->second < table->primary_keys_size) {
      Value value;
      ExprToValue(select_stmt->whereClause->expr2, value);
      ValueCast(value, table->columns[column_it->second].type);
      where_key.primary_keys[column_it->second] = value;
      number_of_primary_keys_in_select_stmt++;
    } else {
      Value value;
      ExprToValue(select_stmt->whereClause->expr2, value);
      ValueCast(value, table->columns[column_it->second].type);
      where_entry.values[column_it->second - table->primary_keys_size] = value;
    }
  }
  if (number_of_primary_keys_in_select_stmt != table->primary_keys_size) {
    goto insert_to_query_cache;
  }

  for (const auto &row : parsed_result.rows) {
    CacheKey key = where_key;
    CacheEntry entry = where_entry;
    for (size_t i = 0; i < row.size(); ++i) {
      Value value = row[i];
      if (!ValueCast(value, table->columns[index[i]].type)) {
        LOG(WARNING) << "Failed to cast value: " << ValueToString(value)
                     << " to type: " << table->columns[index[i]].type
                     << std::endl;
        goto insert_to_query_cache;
      }
      if (index[i] < table->primary_keys_size) {
        key.primary_keys[index[i]] = value;
      } else {
        entry.values[index[i] - table->primary_keys_size] = value;
      }
    }
    cache->Add(key, entry, false, false);
  }

  // table_cache.Dump(cache);

insert_to_query_cache:
  auto query_and_result = std::make_shared<QueryAndResult>(
      std::move(parsed_result), std::move(parse_result));
  select_stmt =
      query_and_result->GetSelectStatement();  // update select_stmt pointer

  table_query_caches_.insert(
      std::make_pair(select_stmt->fromTable->getName(), TableQueryCache()));

  // index
  const auto &where = select_stmt->whereClause;
  if (where->type == hsql::kExprOperator &&
          (where->opType == hsql::kOpBetween &&
           where->expr->type == hsql::kExprColumnRef &&
           (*where->exprList)[0]->type == hsql::kExprLiteralInt &&
           (*where->exprList)[1]->type == hsql::kExprLiteralInt) ||
      (where->opType == hsql::kOpEquals &&
       where->expr->type == hsql::kExprColumnRef &&
       where->expr2->type == hsql::kExprLiteralInt)) {
    auto table_it = table_cache.tables_.find(select_stmt->fromTable->getName());
    if (table_it == table_cache.tables_.end()) {
      LOG(ERROR) << "WhereMatch: Table not found: "
                 << select_stmt->fromTable->getName() << std::endl;
    }
    auto &table = table_it->second;

    auto column_it = table.columns_name_to_index.find(where->expr->name);
    if (column_it == table.columns_name_to_index.end()) {
      LOG(ERROR) << "WhereMatch: Column not found: " << where->expr->name
                 << std::endl;
    }
    auto column = table.columns[column_it->second];

    if (column.type == kLL) {
      Value lower_bound, upper_bound;
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
      } else {
        if (!ExprToValue(where->expr2, lower_bound)) {
          LOG(ERROR) << "WhereMatch: ExprToValue failed" << std::endl;
        }
        if (!ValueCast(lower_bound, kLL)) {
          LOG(ERROR) << "WhereMatch: ValueCast failed" << std::endl;
        }
        upper_bound = lower_bound;
      }

      table_query_caches_.visit(
          select_stmt->fromTable->getName(), [&](auto &table_query_cache_it) {
            auto &[_, table_query_cache] = table_query_cache_it;
            table_query_cache.column_range_indices.insert(std::make_pair(
                where->expr->name, std::make_unique<QueryCacheRangeIndex>()));
            table_query_cache.column_range_indices.visit(
                where->expr->name, [&](auto &column_range_index_it) {
                  auto &[_, column_range_index] = column_range_index_it;
                  column_range_index->Insert(query_and_result,
                                             std::get<kLL>(lower_bound),
                                             std::get<kLL>(upper_bound));
                });
          });
    }
    // TODO: add unhandled types to a standalone index for iteration
  }

  // insert to query cache
  table_query_caches_.visit(
      select_stmt->fromTable->getName(), [&](auto &table_query_cache_it) {
        auto &[_, table_query_cache] = table_query_cache_it;
        table_query_cache.where_query_caches.insert(
            std::make_pair(where_stream.str(), WhereQueryCache()));
        table_query_cache.where_query_caches.visit(
            where_stream.str(), [&](auto &where_query_cache_it) {
              auto &[_, where_query_cache] = where_query_cache_it;
              where_query_cache.query_and_results.insert(
                  std::make_pair(query, query_and_result));
            });
      });
}

void QueryCache::BuildRelationsBetweenQueryAndCachedRows() {
  // TODO
}

std::optional<Packet> QueryCache::ServeSelect(const std::string &query) {
  hsql::SQLParserResult parse_result;
  hsql::SQLParser::parse(query, &parse_result);
  if (!parse_result.isValid()) {
    LOG(ERROR) << "Query Cache ServeSelect: unable to parse query: " << query
               << std::endl
               << "\t" << parse_result.errorMsg() << " L"
               << parse_result.errorLine() << ':' << parse_result.errorColumn()
               << std::endl;
    return std::nullopt;
  }
  if (parse_result.size() != 1) {
    LOG(WARNING) << "Query Cache ServeSelect: multiple queries in one query "
                    "string: "
                 << query << std::endl;
    return std::nullopt;
  }
  if (parse_result.getStatement(0)->type() !=
      hsql::StatementType::kStmtSelect) {
    LOG(WARNING) << "Query Cache ServeSelect: not a select query: " << query
                 << std::endl;
    return std::nullopt;
  }

  auto select_stmt =
      dynamic_cast<const hsql::SelectStatement *>(parse_result.getStatement(0));

  std::stringstream where_stream;
  if (select_stmt->whereClause != nullptr)
    printExpression(where_stream, select_stmt->whereClause, 0);
  std::optional<Packet> ret = std::nullopt;
  table_query_caches_.visit(
      select_stmt->fromTable->getName(), [&](auto &table_query_cache_it) {
        auto &[_, table_query_cache] = table_query_cache_it;
        table_query_cache.where_query_caches.visit(
            where_stream.str(), [&](auto &where_query_cache_it) {
              auto &[_, where_query_cache] = where_query_cache_it;
              where_query_cache.query_and_results.visit(
                  query, [&](auto &query_and_result_it) {
                    auto &[_, query_and_result] = query_and_result_it;
                    std::shared_lock query_and_result_lock(
                        *(query_and_result->mutex_ptr));
                    Packet result_packet;
                    result_packet.buffer = query_and_result->result.Serialize();
                    ret = std::make_optional<Packet>(result_packet);
                  });
            });
      });
  return ret;
}

bool QueryCache::SendQueryToFull(
    Query_cache_block *query_cache_block_full_ptr) {
  // LOG(INFO) << "Sending query to full: " << query_cache_block_full_ptr
  //           << std::endl;
  if (write(lite_to_full_fd_, &query_cache_block_full_ptr,
            sizeof(Query_cache_block *)) == -1) {
    PLOG(ERROR) << "write lite_to_full_fd_" << std::endl;
    return false;
  }

  return true;
}

void *QueryCache::FullListenerThreadBody(void *arg_self) {
  QueryCache *self = static_cast<QueryCache *>(arg_self);

  event_base_loop(self->full_listener_base_, 0);

  return NULL;
}

void QueryCache::FullListenerHandler(int fd, short which, void *arg_self) {
  QueryCache *self = static_cast<QueryCache *>(arg_self);

  // TODO: async
  pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, NULL);
  Query_cache_block *query_cache_block_full_ptr;
  ssize_t bytes_transferred;
  bytes_transferred =
      read(fd, &query_cache_block_full_ptr, sizeof(Query_cache_block *));
  if (bytes_transferred == -1) {
    PLOG(ERROR) << "read full_to_lite_fd_" << std::endl;
    return;
  } else if (bytes_transferred == 0) {
    LOG(INFO) << "full_to_lite_fd_ closed" << std::endl;
    event_del(&self->full_listener_event_);
    return;
  }
  self->mysql_.AssignNewNormalTask(
      {.type = NormalTask::Type::kInsertCache,
       .query_cache_block_full_ptr = query_cache_block_full_ptr});
  pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL);
  // LOG(INFO) << "QueryCache::FullListenerHandler: " <<
  // query_cache_block_full_ptr
  //           << std::endl;
}

void QueryCache::HandleInvalidatedQueryBlockFromFull(
    Query_cache_block *query_cache_block_full_ptr, TableCache &table_cache,
    Cache *cache) {
  // LOG(INFO) << "QueryCache::HandleInvalidatedQueryBlockFromFull: "
  //           << query_cache_block_full_ptr << std::endl;
  AddQueryCacheBlock(reinterpret_cast<Query_cache_block *>(
                         reinterpret_cast<uchar *>(query_cache_block_full_ptr) +
                         shm_v_offset_),
                     table_cache, cache);
  SendQueryToFull(query_cache_block_full_ptr);
}

void QueryCache::InvalidateUnprocessableUpdateDuringNormal(
    const hsql::UpdateStatement *stmt, TableCache &table_cache) {
  if (stmt->table->type != hsql::kTableName) return;
  auto table_it = table_cache.tables_.find(stmt->table->name);
  if (table_it == table_cache.tables_.end()) {
    LOG(WARNING) << "Table not found: " << stmt->table->name << std::endl;
    return;
  }
  auto &table = table_it->second;

  bool unprocessable = stmt->where->type != hsql::kExprOperator ||
                       stmt->where->opType != hsql::kOpEquals ||
                       stmt->where->expr->type != hsql::kExprColumnRef;

  auto column_it = table.columns_name_to_index.find(stmt->where->expr->name);
  if (column_it == table.columns_name_to_index.end()) {
    LOG(WARNING) << "Column not found: " << stmt->where->expr->name
                 << std::endl;
    return;
  }
  unprocessable |= column_it->second >= table.primary_keys_size;
  unprocessable |= table.primary_keys_size != 1;

  if (unprocessable) {
    std::stringstream ss;
    printStatementInfo(ss, stmt);
    table_query_caches_.erase(stmt->table->name);
    // TODO: delete from index
    LOG(INFO) << "Invalidated query cache for table: " << stmt->table->name
              << std::endl
              << " with query:" << ss.str() << std::endl;
    return;
  }

  // TODO: also check updated columns

  CacheKey key;
  CacheEntry old_entry;
  // CacheEntry new_entry; // NOTE: don't need it right now, but is usable if
  // more queries are unprocessable
  key.table = stmt->table->name;
  key.primary_keys.resize(table.primary_keys_size);
  old_entry.values.resize(table.values_size);
  ExprToValue(stmt->where->expr2, key.primary_keys[column_it->second]);

  if (table.columns[column_it->second].type != kLL) {
    LOG(WARNING) << "Unsupported type for query index: "
                 << table.columns[column_it->second].type << std::endl;
    return;
  }
  table_query_caches_.visit(stmt->table->name, [&](auto &table_query_cache_it) {
    auto &[_, table_query_cache] = table_query_cache_it;
    table_query_cache.column_range_indices.visit(
        stmt->where->expr->name, [&](auto &column_range_index_it) {
          auto &[_, column_range_index] = column_range_index_it;
          auto related_query_and_results = column_range_index->Query(
              std::get<kLL>(key.primary_keys[column_it->second]));
          for (const auto &related_query_and_result :
               related_query_and_results) {
            std::unique_lock query_and_result_lock(
                *(related_query_and_result->mutex_ptr));

            // remove from index
            column_range_index->Delete(related_query_and_result);

            // remove from query cache
            auto where_str = related_query_and_result->GetWhereClause();
            table_query_cache.where_query_caches.erase(where_str);
          }
        });
  });

  // table_query_caches_.visit(stmt->table->name, [&](auto
  // &table_query_cache_it) {
  //   auto &[_, table_query_cache] = table_query_cache_it;
  //   table_query_cache.where_query_caches.erase_if(
  //       [&](auto &where_query_cache_it) {
  //         auto &[_, where_query_cache] = where_query_cache_it;
  //         bool ret = false;
  //         where_query_cache.query_and_results.cvisit_while(
  //             [&](const auto &query_and_result_it) {
  //               auto &[_, query_and_result] = query_and_result_it;
  //               std::shared_lock query_and_result_lock(
  //                   *(query_and_result->mutex_ptr));
  //               auto old_match = table_cache.WhereMatch(
  //                   key, old_entry,
  //                   query_and_result->GetSelectStatement()->whereClause);
  //               if (!old_match.has_value() || old_match.value()) {
  //                 ret = true;
  //               } else {
  //                 ret = false;
  //               }
  //               return false;
  //             });
  //         return ret;
  //       });
  // });
}

void QueryCache::InvalidateUnprocessableDeleteDuringNormal(
    const hsql::DeleteStatement *stmt, TableCache &table_cache) {
  auto table_it = table_cache.tables_.find(stmt->tableName);
  if (table_it == table_cache.tables_.end()) {
    LOG(WARNING) << "Table not found: " << stmt->tableName << std::endl;
    return;
  }
  auto &table = table_it->second;

  bool unprocessable = stmt->expr->type != hsql::kExprOperator ||
                       stmt->expr->opType != hsql::kOpEquals ||
                       stmt->expr->expr->type != hsql::kExprColumnRef;

  auto column_it = table.columns_name_to_index.find(stmt->expr->expr->name);
  if (column_it == table.columns_name_to_index.end()) {
    LOG(WARNING) << "Column not found: " << stmt->expr->expr->name << std::endl;
    return;
  }
  unprocessable |= column_it->second >= table.primary_keys_size;
  unprocessable |= table.primary_keys_size != 1;

  if (unprocessable) {
    std::stringstream ss;
    printStatementInfo(ss, stmt);
    table_query_caches_.erase(stmt->tableName);
    LOG(INFO) << "Invalidated query cache for table: " << stmt->tableName
              << std::endl
              << " with query:" << ss.str() << std::endl;
    return;
  }

  // TODO: also check updated columns

  CacheKey key;
  CacheEntry old_entry;
  // CacheEntry new_entry; // NOTE: don't need it right now, but is usable if
  // more queries are unprocessable
  key.table = stmt->tableName;
  key.primary_keys.resize(table.primary_keys_size);
  old_entry.values.resize(table.values_size);
  ExprToValue(stmt->expr->expr2, key.primary_keys[column_it->second]);

  if (table.columns[column_it->second].type != kLL) {
    LOG(WARNING) << "Unsupported type for query index: "
                 << table.columns[column_it->second].type << std::endl;
    return;
  }
  table_query_caches_.visit(stmt->tableName, [&](auto &table_query_cache_it) {
    auto &[_, table_query_cache] = table_query_cache_it;
    table_query_cache.column_range_indices.visit(
        stmt->expr->expr->name, [&](auto &column_range_index_it) {
          auto &[_, column_range_index] = column_range_index_it;
          auto related_query_and_results = column_range_index->Query(
              std::get<kLL>(key.primary_keys[column_it->second]));
          for (const auto &related_query_and_result :
               related_query_and_results) {
            std::unique_lock query_and_result_lock(
                *(related_query_and_result->mutex_ptr));

            // remove from index
            column_range_index->Delete(related_query_and_result);

            // remove from query cache
            auto where_str = related_query_and_result->GetWhereClause();
            table_query_cache.where_query_caches.erase(where_str);
          }
        });
  });

  // table_query_caches_.visit(stmt->tableName, [&](auto &table_query_cache_it)
  // {
  //   auto &[_, table_query_cache] = table_query_cache_it;
  //   table_query_cache.where_query_caches.erase_if(
  //       [&](auto &where_query_cache_it) {
  //         auto &[_, where_query_cache] = where_query_cache_it;
  //         bool ret = false;
  //         where_query_cache.query_and_results.cvisit_while(
  //             [&](const auto &query_and_result_it) {
  //               auto &[_, query_and_result] = query_and_result_it;
  //               std::shared_lock query_and_result_lock(
  //                   *(query_and_result->mutex_ptr));
  //               auto old_match = table_cache.WhereMatch(
  //                   key, old_entry,
  //                   query_and_result->GetSelectStatement()->whereClause);
  //               if (!old_match.has_value() || old_match.value()) {
  //                 ret = true;
  //               } else {
  //                 ret = false;
  //               }
  //               return false;
  //             });
  //         return ret;
  //       });
  // });
}