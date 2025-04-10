#pragma once

#include <pthread.h>
#include <barrier>
#include <bpf/bpf.h>
#include <event2/event.h>
#include <event2/util.h>
#include <unordered_map>
#include <utility>  // for std::pair
#include <glog/logging.h>  // for PCHECK and LOG
#include <filesystem>
#include <regex>
#include <vector>
#include <iostream>
#include <fstream>
#include <memory>
#include <sstream>
#include <sys/syscall.h>
#include <unistd.h>
#include <dirent.h>

#include "litesys.skel.h"
#include "connection.hpp"
#include "core.hpp"
#include "thread_safe_queue.hpp"
#include "thread_safe_set.hpp"
#include "worker.hpp"

#define ACCEPT 1
#define CLOSE 2
#define MAX_MSG_SIZE 256
#define MAX_PKT_SIZE 4096
namespace lite {

// Add hash function for std::pair before the structs
struct PairHash {
  template <class T1, class T2>
  std::size_t operator()(const std::pair<T1, T2>& p) const {
    auto h1 = std::hash<T1>{}(p.first);
    auto h2 = std::hash<T2>{}(p.second);
    auto hash = h1 ^ (h2 << 1);
    return hash;  // Combine the hash values
  }
};

struct event_type_header {
    int kind;  // ACCEPT or CLOSE
};

struct tcp_info {
    uint8_t proto;
    uint32_t saddr;
    uint16_t sport;
    uint32_t daddr;
    uint16_t dport;
    uint8_t state;  // TCP connection state
} __attribute__((packed));

struct ConnectionEvent {
    struct event_type_header header;
    struct tcp_info connection;
} __attribute__((packed));

struct ParseResult{
    std::unique_ptr<char[]> src_ip;
    uint16_t src_port;
    std::unique_ptr<char[]> dst_ip;
    uint16_t dst_port;
    std::unique_ptr<uint8_t[]> payload;
    int len;
    bool request_dir;
    int seq_num;
    // connection
};

struct packet_data {
  uint32_t len;
  uint32_t saddr;
  uint32_t daddr;
  uint16_t sport;
  uint16_t dport;
  uint32_t seq_num;
  char direction;  // 'R' for recv, 'S' for send
  unsigned char data[MAX_PKT_SIZE];
} __attribute__((packed));

struct socket_info {
  uint64_t socket_fd;
  uint64_t pid;
  uint64_t ref_socket_fd;
};

struct socket_data_event_t
{
  unsigned int pid;
  int fd;
  bool is_connection;
  int socket_fd;
  bool is_read;
  unsigned int msg_size;
  char msg[MAX_MSG_SIZE];
  uint32_t seq_num;
} __attribute__((packed));

template <typename Application, typename Request, typename Response,
          typename ConnectionInfo, typename CacheKey, typename CacheEntry>
class EbpfWorker : public Worker<Application, Request, Response,
                                 ConnectionInfo, CacheKey, CacheEntry> {
  using ConnectionInstance = Connection<Application, Request, Response,
                                        ConnectionInfo, CacheKey, CacheEntry>;
  using LiteCoreInstance = LiteCore<Application, Request, Response,
                                    ConnectionInfo, CacheKey, CacheEntry>;
  using WorkerBase = Worker<Application, Request, Response,
                            ConnectionInfo, CacheKey, CacheEntry>;

 public:
  explicit EbpfWorker(LiteCoreInstance &lite_core,
                  std::barrier<std::function<void()>> &barrier);

  ~EbpfWorker();

  /// Create the worker thread and start running the event loop.
  int Run(const char name[] = "lite-worker");


  //Set Mode
  int SetMode(int mode);

  //Set Socket Info
  int SetSocketInfo(uint64_t socket_fd, uint64_t pid);

  /// The file descriptor used to signal the worker thread.
  evutil_socket_t notify_event_fd;

  /// The connections managed by the worker thread.
  ThreadSafeSet<ConnectionInstance *> conns_;

  /// Source Address, port to connection map
  std::unordered_map<std::pair<uint32_t, uint16_t>, ConnectionInstance *, PairHash> source_to_conn_;

  int count;

 private:
  /// PID of the worker thread.
  pthread_t thread_id_;

  /// The event base for the worker thread.
  struct event_base *base_;

  /// The event used to notify the worker thread.
  struct event notify_event_;

  struct litesys_bpf *skel;


  struct ring_buffer *rb;

  struct ring_buffer *pb;

  unsigned char *buffer;

  int prog_fd_;

  /// The underlying service implementation.
  LiteCoreInstance &lite_core_;

  std::barrier<std::function<void()>> &barrier_;

  socket_info findSocketFD(int port);
  
  std::string executeCommand(const std::string& command);

  int get_pid_by_port(int port);

  int get_socket_fd_from_pid(int pid, int target_fd);

  /// The entry point for the worker thread.
  static void *ThreadBody(void *arg_self);

  /// Handle a new notification.
  static void NotifyHandler(evutil_socket_t fd, short which, void *arg_self);

  /// Timer handler for ring buffer polling
  static void TimerHandler(evutil_socket_t fd, short events, void* arg);

  static int HandleConnection(void *ctx, void *data, size_t data_sz);

  static int HandlePacket(void *ctx, void *data, size_t data_sz);
  
  /// Parse the TCP data.
  static ParseResult parse_tcp_data(unsigned char *buffer, int size);

  /// Check the direction of the TCP data.
  static bool check_dir(char* src_ip, uint16_t sport, char* dst_ip, uint16_t dport);

  /// Convert the port to network order.
  uint16_t htons_custom(uint16_t i);

  /// Convert the IP address to uint32_t.
  uint32_t ipToUint32(const std::string& ip);
};

}  // namespace lite
