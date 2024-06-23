#include <getopt.h>

#include <boost/lexical_cast.hpp>
#include <boost/thread/thread.hpp>
#include <iostream>
#include <lite.hpp>
#include <string>

#include "packet.hpp"
#include "service.hpp"

void PrintHelp() {
  LOG(INFO) << "Usage: LiteProxy [-t thread_num] [-s size] \n"
               "       [-p port] [-h help]\n";
  LOG(INFO) << "Options:\n";
  LOG(INFO) << "  -t, --thread_num      Number of worker threads. Default: \n"
               "                          hardware_concurrency() - 1\n";
  LOG(INFO) << "  -p, --port            Default: 6379\n";
  LOG(INFO) << "  -h, --help            Show this help message.\n";
  LOG(INFO) << "Example:\n";
  LOG(INFO) << "  LiteProxy -t 128 -p 6379\n";
  LOG(INFO) << "  LiteProxy --thread_num=4\n";
}

int main(int argc, char* argv[]) {
  FLAGS_logtostderr = 1;
  FLAGS_logbufsecs = 0;
  google::InitGoogleLogging(argv[0]);
  try {
    size_t thread_pool_size = boost::thread::hardware_concurrency() - 1;
    size_t cache_size(1024);
    const char* port = "11211";

    const char* const short_opts = "t:p:a:h";
    const option long_opts[] = {{"thread_num", required_argument, nullptr, 't'},
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

    LOG(INFO) << "LiteDatanode starts" << std::endl;
    LOG(INFO) << "\tlistening on port: " << port << std::endl;
    LOG(INFO) << "\tthread_pool_size: " << thread_pool_size << std::endl;

    // Initialise the server.
    // TODO: make address and port configurable.
    std::string backend_addr = "namenode";
    std::string backend_port = "8022";
    Datanode datanode;
    lite::LiteServer<Datanode, Packet, Packet, ConnectionInfo, std::string,
                     CacheEntry>
        s(thread_pool_size, cache_size, datanode, backend_addr, backend_port,
          1000ms, 20000, 0.9, 1, "/tmp/LiteDatanode");

    // Run the server until stopped.
    if (!s.Run(port)) {
      LOG(FATAL) << "Failed to start server" << std::endl;
    }
  } catch (std::exception& e) {
    LOG(FATAL) << "exception: " << e.what() << "\n";
  }

  return 0;
}
