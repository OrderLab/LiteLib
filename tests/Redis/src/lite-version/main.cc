#include <getopt.h>

#include <boost/lexical_cast.hpp>
#include <boost/thread/thread.hpp>
#include <iostream>
#include <lite.hpp>
#include <string>

#include "packet.hpp"
#include "service.hpp"

void PrintHelp() {
  std::cout << "Usage: LiteRedis [-h hostname] [-p port]\n";
  std::cout << "Options:\n";
  std::cout << "  -h <hostname>\tServer hostname (default: 127.0.0.1).\n";
  std::cout << "  -p <port>\tServer port (default: 6379).\n";
  std::cout << "  -H <help>\tOutput this help and exit.\n";
}

int main(int argc, char *argv[]) {
  try {
    // size_t thread_pool_size = boost::thread::hardware_concurrency() - 1;
    size_t thread_pool_size = 1;
    size_t cache_size(1024);
    const char *port = "6479";
    const char *address = "127.0.0.1";
    const char *const short_opts = "p:h:H";
    const option long_opts[] = {{"port", required_argument, nullptr, 'p'},
                                {"address", required_argument, nullptr, 'h'},
                                {"help", no_argument, nullptr, 'H'},
                                {0, 0, 0, 0}};
    int opt;
    while ((opt = getopt_long(argc, argv, short_opts, long_opts, nullptr)) !=
           -1) {
      switch (opt) {
        case 'p':
          port = optarg;
          break;
        case 'a':
          break;
        case 'h':
          address = optarg;
          break;
        default:
          PrintHelp();
          return 0;
      }
    }

    std::cout << "LiteRedis starts" << std::endl;
    std::cout << "\tlistening on address: " << address << std::endl;
    std::cout << "\tlistening on port: " << port << std::endl;

    // Initialise the server.
    // TODO: make address and port configurable.
    std::string backend_addr = "172.16.0.2";
    std::string backend_port = "6379";
    Redis redis;
    lite::LiteServer<Redis, Packet, Packet, ConnectionInfo, std::string,
                     CacheEntry>
        s(thread_pool_size, cache_size, redis, backend_addr, backend_port,
          "/tmp/lite_Redis");
    // Run the server until stopped.
    s.Run(port);
  } catch (std::exception &e) {
    std::cerr << "exception: " << e.what() << "\n";
  }

  return 0;
}
