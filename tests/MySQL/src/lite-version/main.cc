#include <getopt.h>

#include <boost/lexical_cast.hpp>
#include <boost/thread/thread.hpp>
#include <iostream>
#include <lite.hpp>
#include <string>

#include "packet.hpp"
#include "service.hpp"

void PrintHelp() {
  LOG(INFO) << "Usage: LiteMySQL [-t thread_num] [-s size] \n"
               "       [-p port] [-h help]\n";
  LOG(INFO) << "Options:\n";
  LOG(INFO) << "  -t, --thread_num      Number of worker threads. Default: \n"
               "                          hardware_concurrency() - 1\n";
  LOG(INFO)
      << "  -s, --size            Max number of items in cache. Default: \n"
         "                          1024\n";
  LOG(INFO) << "  -p, --port            Default: 3306\n";
  LOG(INFO) << "  -h, --help            Show this help message.\n";
  LOG(INFO) << "Example:\n";
  LOG(INFO) << "  LiteMySQL -t 128 -p 3306\n";
  LOG(INFO) << "  LiteMySQL --thread_num=4\n";
}

int main(int argc, char* argv[]) {
  google::InitGoogleLogging(argv[0]);
  try {
    size_t thread_pool_size = boost::thread::hardware_concurrency() - 1;
    size_t cache_size(1024);
    const char* port = "3306";

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

    LOG(INFO) << "LiteMySQL starts" << std::endl;
    LOG(INFO) << "\tlistening on port: " << port << std::endl;
    LOG(INFO) << "\tthread_pool_size: " << thread_pool_size << std::endl;
    LOG(INFO) << "\tsize: " << cache_size << std::endl;

    // Initialise the server.
    // TODO: make address and port configurable.
    std::string backend_addr = "localhost";
    std::string backend_port = "60000";
    MySQL mysql;
    lite::LiteServer<MySQL, Packet, Packet, ConnectionInfo, std::string,
                     CacheEntry>
        s(thread_pool_size, cache_size, mysql, backend_addr, backend_port,
          1000ms, 20000, 0.9, 1, "/tmp/lite_mysql");

    // Run the server until stopped.
    if (!s.Run(port)) {
      LOG(FATAL) << "Failed to start server" << std::endl;
    }
  } catch (std::exception& e) {
    LOG(FATAL) << "exception: " << e.what() << "\n";
  }

  return 0;
}
