#include "query_cache.hpp"

#include <fcntl.h>
#include <hsql/SQLParser.h>
#include <hsql/util/sqlhelper.h>
#include <sys/mman.h>
#include <sys/stat.h>

#include <lite.hpp>

#include "mysql-server/sql_cache.hpp"

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

bool QueryCache::Init() {
  int shm_fd = shm_open(query_cache_shm_name, O_RDONLY, 0666);
  if (shm_fd == -1) {
    PLOG(ERROR) << "Unable to open query cache shared memory";
    return false;
  }

  // Get the size of the shared memory object
  struct stat sb;
  if (fstat(shm_fd, &sb) == -1) {
    PLOG(ERROR) << "Unable to get the size of query cache shared memory";
    return false;
  }
  size_t shm_size = sb.st_size;
  LOG(INFO) << "Size of query cache shared memory: " << shm_size << " bytes"
            << std::endl;

  uchar *ptr =
      static_cast<uchar *>(mmap(0, shm_size, PROT_READ, MAP_SHARED, shm_fd, 0));
  if (ptr == MAP_FAILED) {
    PLOG(ERROR) << "Unable to map query cache shared memory";
    return false;
  }
  LOG(INFO) << "Query cache shared memory mapped at address "
            << static_cast<void *>(ptr) << std::endl;

  AlignedShmInfo *info = reinterpret_cast<AlignedShmInfo *>(ptr);
  // LOG(INFO) << info->shm_info << std::endl;

  if (info->shm_info.queries_blocks == 0) {
    LOG(WARNING) << "No query cache block in shared memory" << std::endl;
    return true;
  }

  ptrdiff_t v_offset = ptr - info->shm_info.vaddr;

  LOG(INFO) << "Offset of query cache shared memory: 0x" << std::hex << v_offset
            << std::endl;

  auto query_block_ptr =
      info->shm_info.queries_blocks_with_vaddr_offset(v_offset);
  const auto query_block_linked_list_head = query_block_ptr;

  do {
    std::vector<uint8_t> result;
    const auto query_ptr = query_block_ptr->query_with_vaddr_offset(v_offset);
    LOG(INFO) << "Query: " << query_ptr->query_with_vaddr_offset(v_offset)
              << std::endl;
    auto result_block_ptr = query_ptr->result_with_vaddr_offset(v_offset);
    const auto result_block_linked_list_head = result_block_ptr;
    do {
      const auto result_ptr =
          result_block_ptr->result_with_vaddr_offset(v_offset);
      // LOG(INFO) << "  Result len: " << result_block_ptr->result_data_len()
      //           << std::endl;
      // LOG(INFO) << "  Result content: ";
      auto result_data = result_ptr->data_with_vaddr_offset(v_offset);
      result.insert(result.end(), result_data,
                    result_data + result_block_ptr->result_data_len());
      // for (size_t i = 0; i < result_block_ptr->result_data_len(); ++i) {
      //   result.push_back(result_data[i]);
      //   // std::cerr << result_ptr->data_with_vaddr_offset(v_offset)[i];
      // }
      // TODO: reduce the number of memcpy
    } while ((result_block_ptr = result_block_ptr->next_with_vaddr_offset(
                  v_offset)) != result_block_linked_list_head);
    AddQueryAndResult((char *)query_ptr->query_with_vaddr_offset(v_offset),
                      result);
  } while ((query_block_ptr = query_block_ptr->next_with_vaddr_offset(
                v_offset)) != query_block_linked_list_head);

  if (munmap(ptr, shm_size) == -1) {
    PLOG(ERROR) << "Unable to unmap query cache shared memory";
    return false;
  }

  if (close(shm_fd) == -1) {
    PLOG(ERROR) << "Unable to close query cache shared memory";
    return false;
  }

  BuildRelationsBetweenQueryAndCachedRows();

  return true;
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
  if (query_cache_[select_stmt->fromTable->getName()].find(
          where_stream.str()) ==
      query_cache_[select_stmt->fromTable->getName()].end()) {
    query_cache_[select_stmt->fromTable->getName()][where_stream.str()] =
        TableQueryCacheEntry{
            .where = select_stmt->whereClause,
            .result_table = std::make_shared<
                std::unordered_map<std::string, ResultTableEntry>>()};
  };
  (*query_cache_[select_stmt->fromTable->getName()][where_stream.str()]
        .result_table)[query] = ResultTableEntry{
      .select = select_stmt,
      .result = Result::Deserialize(result),
  };
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
  const auto &result_it =
      table_query_cache_it->second.result_table->find(query);
  if (result_it == table_query_cache_it->second.result_table->end())
    return std::nullopt;
  Packet result_packet;
  result_packet.buffer = result_it->second.result.Serialize();
  return std::make_optional<Packet>(result_packet);
}