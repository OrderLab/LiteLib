#include <getopt.h>

#include <boost/lexical_cast.hpp>
#include <boost/thread/thread.hpp>
#include <iostream>
#include <lite.hpp>
#include <string>

#include "packet.hpp"
#include "service.hpp"

void PrintHelp() {
  LOG(INFO) << "Usage: LiteMemcached [-t thread_num] [-s size] \n"
               "       [-p port] [-h help]\n";
  LOG(INFO) << "Options:\n";
  LOG(INFO) << "  -t, --thread_num      Number of worker threads. Default: \n"
               "                          hardware_concurrency() - 1\n";
  LOG(INFO)
      << "  -s, --size            Max number of items in cache. Default: \n"
         "                          1024\n";
  LOG(INFO) << "  -p, --port            Default: 11211\n";
  LOG(INFO) << "  -h, --help            Show this help message.\n";
  LOG(INFO) << "Example:\n";
  LOG(INFO) << "  LiteMemcached -t 128 -m 1GiB -p 11211\n";
  LOG(INFO) << "  LiteMemcached --thread_num=4 --size=256\n";
}

int main(int argc, char* argv[]) {
  try {
    size_t thread_pool_size = boost::thread::hardware_concurrency() - 1;
    size_t cache_size(1024);
    const char* port = "11211";

    const char* const short_opts = "t:s:p:a:h";
    const option long_opts[] = {{"thread_num", required_argument, nullptr, 't'},
                                {"size", required_argument, nullptr, 's'},
                                {"port", required_argument, nullptr, 'p'},
                                {"address", required_argument, nullptr, 'a'},
                                {"help", no_argument, nullptr, 'h'},
                                {0, 0, 0, 0}};
    int opt;
    while ((opt = getopt_long(argc, argv, short_opts, long_opts, nullptr)) !=
           -1) {
      switch (opt) {
        case 't':
          thread_pool_size = boost::lexical_cast<size_t>(optarg);
          break;
        case 's':
          cache_size = boost::lexical_cast<size_t>(optarg);
          break;
        case 'p':
          port = optarg;
          break;
        case 'a':
          break;
        case 'h':
        default:
          PrintHelp();
          return 0;
      }
    }

    LOG(INFO) << "LiteMemcached starts" << std::endl;
    LOG(INFO) << "\tlistening on port: " << port << std::endl;
    LOG(INFO) << "\tthread_pool_size: " << thread_pool_size << std::endl;
    LOG(INFO) << "\tsize: " << cache_size << std::endl;

    // Initialise the server.
    // TODO: make address and port configurable.
    std::string backend_addr = "localhost";
    std::string backend_port = "60000";
    Memcached memcached;
    lite::LiteServer<Memcached, Packet, Packet, ConnectionInfo,
                     std::vector<uint8_t>, CacheEntry>
        s(thread_pool_size, cache_size, memcached, backend_addr, backend_port,
          "/tmp/lite_memcached");

    // Run the server until stopped.
    s.Run(port);
  } catch (std::exception& e) {
    std::cerr << "exception: " << e.what() << "\n";
  }

  return 0;
}
