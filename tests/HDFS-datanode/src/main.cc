#include <getopt.h>

#include <boost/lexical_cast.hpp>
#include <boost/thread/thread.hpp>
#include <iostream>
#include <lite.hpp>
#include <string>

#include "packet.hpp"
#include "service.hpp"

void SuppressProtobufLogging() {
    google::protobuf::LogSilencer* log_silencer = new google::protobuf::LogSilencer();
    google::protobuf::SetLogHandler([](google::protobuf::LogLevel level, const char* filename, int line, const std::string& message) {
        // Silencing logs
    });
}

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

void server_thread_body(
    lite::LiteServer<Datanode, Packet, Packet, ConnectionInfo, std::string,
                     CacheEntry>* server,
    const char* port) {
  if (!server->Run(port)) {
    LOG(FATAL) << "Failed to start server" << std::endl;
  }
}

int main(int argc, char* argv[]) {
  SuppressProtobufLogging();
  FLAGS_logtostderr = 1;
  FLAGS_logbufsecs = 0;
  google::InitGoogleLogging(argv[0]);
  try {
    size_t thread_pool_size = boost::thread::hardware_concurrency() - 1;
    size_t cache_size(1024);
    const char* port_1 = "11111";
    const char* port_2 = "22222";
    const char* port_3 = "33333";
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
    std::string backend_addr_3 = "datanode";
    std::string backend_port_3 = "9867";
    Datanode datanode;
    lite::LiteServer<Datanode, Packet, Packet, ConnectionInfo, std::string,
                     CacheEntry>
        s_1(1, cache_size, datanode, backend_addr_1,
            backend_port_1, 1000ms, 20000, 0.9, 1, "/tmp/LiteDatanode",
            true, true);
    lite::LiteServer<Datanode, Packet, Packet, ConnectionInfo, std::string,
                     CacheEntry>
        s_2(thread_pool_size, cache_size, datanode, backend_addr_2,
            backend_port_2, 1000ms, 20000, 0.9, 1, "/tmp/LiteDatanode",
            true);
    lite::LiteServer<Datanode, Packet, Packet, ConnectionInfo, std::string,
                     CacheEntry>
        s_3(thread_pool_size, cache_size, datanode, backend_addr_3,
            backend_port_3, 1000ms, 20000, 0.9, 1, "/tmp/LiteDatanode",
            true);
    // Run the server until stopped.
    datanode.RegisterServer(&s_1);
    boost::thread thread1(server_thread_body, &s_1, port_1);
    boost::thread thread2(server_thread_body, &s_2, port_2);
    boost::thread thread3(server_thread_body, &s_3, port_3);
    thread1.join();
    thread2.join();
    thread3.join();
  } catch (std::exception& e) {
    LOG(FATAL) << "exception: " << e.what() << "\n";
  }

  return 0;
}
