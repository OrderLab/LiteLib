#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>

#include <lite.hpp>

#include "mysql-server/sql_cache.hpp"
#include "service.hpp"

bool MySQL::ParseQueryCache() {
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
    std::shared_ptr<std::vector<uint8_t>> result =
        std::make_shared<std::vector<uint8_t>>();
    const auto query_ptr = query_block_ptr->query_with_vaddr_offset(v_offset);
    // LOG(INFO) << "Query: " << query_ptr->query_with_vaddr_offset(v_offset)
    //           << std::endl;
    auto result_block_ptr = query_ptr->result_with_vaddr_offset(v_offset);
    const auto result_block_linked_list_head = result_block_ptr;
    do {
      const auto result_ptr =
          result_block_ptr->result_with_vaddr_offset(v_offset);
      // LOG(INFO) << "  Result len: " << result_block_ptr->result_data_len()
      //           << std::endl;
      // LOG(INFO) << "  Result content: ";
      auto result_data = result_ptr->data_with_vaddr_offset(v_offset);
      for (size_t i = 0; i < result_block_ptr->result_data_len(); ++i) {
        result->push_back(result_data[i]);
        // std::cerr << result_ptr->data_with_vaddr_offset(v_offset)[i];
      }
      // LOG(INFO) << std::endl;
    } while ((result_block_ptr = result_block_ptr->next_with_vaddr_offset(
                  v_offset)) != result_block_linked_list_head);
    query_cache_[std::string{
        (char *)query_ptr->query_with_vaddr_offset(v_offset)}] = result;
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
  
  return true;
}