#include <getopt.h>

#include <boost/lexical_cast.hpp>
#include <boost/thread/thread.hpp>
#include <iostream>
#include <lite.hpp>
#include <string>

#include "packet.hpp"
#include "service.hpp"

void PrintHelp() {
  std::cout << "Usage: LiteRedis [-s size] [-t thread] [-H help]\n";
  std::cout << "Options:\n";
  std::cout << "  -s <size>\tCache size (default: 16384 * 8).\n";
  std::cout << "  -t <thread>\tThread pool size (default: 1).\n";
  std::cout << "  -H <help>\tOutput this help and exit.\n";
}

int main(int argc, char *argv[]) {
  try {
    // size_t thread_pool_size = boost::thread::hardware_concurrency() - 1;
    size_t thread_pool_size = 4;
    size_t cache_size(16384 * 8);
    size_t shared_memory_size(4ll * 1024 * 1024 * 1024);
    const char *const short_opts = "s:t:H";
    const option long_opts[] = {{"size", required_argument, nullptr, 's'},
                                {"thread", required_argument, nullptr, 't'},
                                {"help", no_argument, nullptr, 'H'},
                                {0, 0, 0, 0}};
    int opt;
    while ((opt = getopt_long(argc, argv, short_opts, long_opts, nullptr)) !=
           -1) {
      switch (opt) {
        case 's':
          cache_size = boost::lexical_cast<size_t>(optarg);
          break;
        case 't':
          thread_pool_size = boost::lexical_cast<size_t>(optarg);
          break;
        default:
          PrintHelp();
          return 0;
      }
    }

    // recovered full addr
    std::string backend_addr = "127.0.0.1";
    std::string backend_port = "16379";

    Redis redis;
    lite::LiteServer<Redis, RESPPacket, RESPPacket, ConnectionInfo, CacheKey,
                     CacheEntry>
        s(thread_pool_size, shared_memory_size, cache_size, redis, backend_addr,
          backend_port, 1000ms, 80000, 0.9, 1, "/tmp/lite_Redis");
    shm = &s.lite_core_.shared_memory_;
    redis.DelayedConstructor();
    // Run the server until stopped.
    s.Run();
  } catch (std::exception &e) {
    std::cerr << "exception: " << e.what() << "\n";
  }

  return 0;
}
