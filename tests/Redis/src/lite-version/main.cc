#include <getopt.h>

#include <boost/lexical_cast.hpp>
#include <boost/thread/thread.hpp>
#include <iostream>
#include <server.hpp>
#include <string>

#include "packet.hpp"
#include "service.hpp"

void PrintHelp() {
    std::cout << "Usage: redis-cli [OPTIONS] [cmd [arg [arg ...]]]\n"
              << "  -h <hostname>      Server hostname (default: 127.0.0.1).\n"
              << "  -p <port>          Server port (default: 6379).\n"
              << "--help             Output this help and exit.\n";
}

int main(int argc, char* argv[]) {
  try {
    size_t thread_pool_size = boost::thread::hardware_concurrency() - 1;
    size_t cache_size(1024);
    const char* port = "6379";

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

    std::cout << "LiteLevelDB starts" << std::endl;
    std::cout << "\tlistening on port: " << port << std::endl;
    std::cout << "\tthread_pool_size: " << thread_pool_size << std::endl;
    std::cout << "\tsize: " << cache_size << std::endl;

    // Initialise the server.
    // TODO: make address and port configurable.
    std::string backend_addr = "localhost";
    std::string backend_port = "60000";
    LevelDB level_db;
    lite::LiteServer<Packet, Packet, LevelDB, std::string, CacheEntry, LogEntry,
                     ConnectionInfo>
        s(thread_pool_size, cache_size, level_db, backend_addr, backend_port,
          "/tmp/lite_LevelDB");

    // Run the server until stopped.
    s.Run(port);
  } catch (std::exception& e) {
    std::cerr << "exception: " << e.what() << "\n";
  }

  return 0;
}
