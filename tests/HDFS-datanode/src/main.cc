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
    const char* port_1 = "11211";
    const char* port_2 = "11212";
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
          port_1 = optarg;
          break;
        case 'a':
          break;
        case 'h':
        default:
          PrintHelp();
          return 0;
      }
    }

    LOG(INFO) << "LiteDatanode_rpc starts" << std::endl;
    LOG(INFO) << "\tlistening on port: " << port_1 << std::endl;
    LOG(INFO) << "\tthread_pool_size: " << thread_pool_size << std::endl;
    LOG(INFO) << "LiteDatanode_tcp starts" << std::endl;
    LOG(INFO) << "\tlistening on port: " << port_2 << std::endl;
    LOG(INFO) << "\tthread_pool_size: " << thread_pool_size << std::endl;

    // Initialise the server.
    // TODO: make address and port configurable.
    std::string backend_addr_1 = "namenode";
    std::string backend_port_1 = "8020";
    std::string backend_addr_2 = "datanode";
    std::string backend_port_2 = "9866";
    Datanode datanode;
    lite::LiteServer<Datanode, Packet, Packet, ConnectionInfo, std::string,
                     CacheEntry>
        s_1(thread_pool_size, cache_size, datanode, backend_addr_1,
            backend_port_1, 1000ms, 20000, 0.9, 1, "/tmp/LiteDatanode_rpc",
            true, true);
    lite::LiteServer<Datanode, Packet, Packet, ConnectionInfo, std::string,
                     CacheEntry>
        s_2(thread_pool_size, cache_size, datanode, backend_addr_2,
            backend_port_2, 1000ms, 20000, 0.9, 1, "/tmp/LiteDatanode_tcp",
            true);
    // Run the server until stopped.
    if (!s_1.Run(port_1)) {
      LOG(FATAL) << "Failed to start server 1" << std::endl;
    }
    if (!s_2.Run(port_2)) {
      LOG(FATAL) << "Failed to start server 2" << std::endl;
    }
  } catch (std::exception& e) {
    LOG(FATAL) << "exception: " << e.what() << "\n";
  }

  return 0;
}
