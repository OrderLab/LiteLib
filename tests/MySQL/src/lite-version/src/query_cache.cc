#include "query_cache.hpp"

#include <fcntl.h>
#include <hsql/SQLParser.h>
#include <hsql/util/sqlhelper.h>
#include <sys/mman.h>
#include <sys/stat.h>

#include <lite.hpp>

#include "service.hpp"

QueryCache::Result QueryCache::Result::Deserialize(
    std::vector<uint8_t> &buffer) {
  Result result;

  const uint8_t *begin = buffer.data();
  for (uint8_t i = 0; i < 3; i++) {
    uint32_t payload_length = begin[0] | begin[1] << 8 | begin[2] << 16;
    result.prefix_packets.insert(result.prefix_packets.end(), begin,
                                 begin + 4 + payload_length);
    begin += 4 + payload_length;
  }

  // TODO: actually parsing the field packets
  result.null_bitmap_length = 1;

  while (true) {
    if (begin[4]) break;  // EOF packet

    Row row;
    begin += 5 + result.null_bitmap_length;
    // TODO: actually parsing the field packets to get the format
    row.push_back(FetchValue(begin, kVARCHAR));

    result.rows.push_back(row);
  }

  result.suffix_packets.insert(result.suffix_packets.end(),
                               buffer.begin() + (begin + 5 - buffer.data()),
                               buffer.end());

  return result;
}

std::shared_ptr<std::vector<uint8_t>> QueryCache::Result::Serialize() {
  auto buffer = std::make_shared<std::vector<uint8_t>>();
  buffer->insert(buffer->end(), prefix_packets.begin(), prefix_packets.end());

  uint8_t packet_number = 4;
  for (const auto &row : rows) {
    std::vector<uint8_t> values_buffer;

    for (size_t i = 0; i < row.size(); ++i) {
      auto value_buffer = ValueToNetworkBuffer(row[i]);
      values_buffer.insert(values_buffer.end(), value_buffer.begin(),
                           value_buffer.end());
    }

    uint32_t row_packet_length = 1 + null_bitmap_length + values_buffer.size();
    // packet length
    buffer->insert(buffer->end(), int2pointer(row_packet_length),
                   int2pointer(row_packet_length) + 3);
    // packet number
    buffer->push_back(packet_number++);
    // response code
    buffer->push_back(0x0);
    // row null buffer
    for (size_t i = 0; i < null_bitmap_length; ++i) {
      buffer->push_back(0x0);
    }
    // values
    buffer->insert(buffer->end(), values_buffer.begin(), values_buffer.end());
  }

  // EOF
  // length
  buffer->push_back(0x5);
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

void QueryCache::EmergencyToNormalHook() { ConnectToFull(); }

bool QueryCache::NormalToEmergencyHook() {
  if (shm_info_->shm_info.queries_blocks == 0) {
    LOG(WARNING) << "No query cache block in shared memory" << std::endl;
    return true;
  }

  auto query_block_ptr =
      shm_info_->shm_info.queries_blocks_with_vaddr_offset(shm_v_offset_);
  const auto query_block_linked_list_head = query_block_ptr;

  do {
    AddQueryCacheBlock(query_block_ptr);
  } while ((query_block_ptr = query_block_ptr->next_with_vaddr_offset(
                shm_v_offset_)) != query_block_linked_list_head);

  DisconnectFromFull();

  BuildRelationsBetweenQueryAndCachedRows();

  return true;
}

void QueryCache::AddQueryCacheBlock(
    Query_cache_block *query_cache_block_lite_ptr) {
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
                    result);
}

void QueryCache::AddQueryAndResult(std::string query,
                                   std::vector<uint8_t> &result) {
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
  query_cache_[select_stmt->fromTable->getName()][where_stream.str()][query] =
      ResultTableEntry(Result::Deserialize(result), std::move(parse_result));
};

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
  auto &table_query_cache = query_cache_[select_stmt->fromTable->getName()];
  const auto &table_query_cache_it = table_query_cache.find(where_stream.str());
  if (table_query_cache_it == table_query_cache.end()) return std::nullopt;
  const auto &result_it = table_query_cache_it->second.find(query);
  if (result_it == table_query_cache_it->second.end()) return std::nullopt;
  Packet result_packet;
  result_packet.buffer = result_it->second.result.Serialize();
  return std::make_optional<Packet>(result_packet);
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
  self->mysql_.notify_queue_.push_back(
      {.type = MySQL::NormalTask::Type::kInsertCache,
       .query_cache_block_full_ptr = query_cache_block_full_ptr});
  uint64_t buf = 1;
  PLOG_IF(ERROR, write(self->mysql_.notify_event_fd_, &buf, sizeof(uint64_t)) !=
                     sizeof(uint64_t))
      << "failed writing to mysql eventfd";
  pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL);
  // LOG(INFO) << "QueryCache::FullListenerHandler: " <<
  // query_cache_block_full_ptr
  //           << std::endl;
}

void QueryCache::HandleInvalidatedQueryBlockFromFull(
    Query_cache_block *query_cache_block_full_ptr) {
  // LOG(INFO) << "QueryCache::HandleInvalidatedQueryBlockFromFull: "
  //           << query_cache_block_full_ptr << std::endl;
  AddQueryCacheBlock(reinterpret_cast<Query_cache_block *>(
      reinterpret_cast<uchar *>(query_cache_block_full_ptr) + shm_v_offset_));
  SendQueryToFull(query_cache_block_full_ptr);
}