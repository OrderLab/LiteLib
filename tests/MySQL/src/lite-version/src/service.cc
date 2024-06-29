#include "service.hpp"

#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>

#include <lite.hpp>

const char query_cache_shm_name[] = "mysql_query_cache";

void MySQL::NormalToEmergencyHook() {
  const int SIZE = 4096;

  int shm_fd = shm_open(query_cache_shm_name, O_RDONLY, 0666);
  if (shm_fd == -1) {
    PLOG(FATAL) << "Unable to open query cache shared memory";
  }

  // Get the size of the shared memory object
  struct stat sb;
  if (fstat(shm_fd, &sb) == -1) {
    perror("fstat");
    exit(EXIT_FAILURE);
  }
  size_t shm_size = sb.st_size;
  PLOG(INFO) << "Size of query cache shared memory: " << shm_size << " bytes"
             << std::endl;

  void *ptr = mmap(0, shm_size, PROT_READ, MAP_SHARED, shm_fd, 0);
  if (ptr == MAP_FAILED) {
    PLOG(FATAL) << "Unable to map query cache shared memory";
  }

  // Read the shared memory
  LOG(INFO) << "First byte read from query cache shared memory: "
            << *static_cast<ulong *>(ptr) << std::endl;
}