#pragma once

#include <pthread.h>

#include <barrier>
#include <bpf/bpf.h>

#include "litesys.skel.h"
#include "connection.hpp"
#include "core.hpp"
#include "thread_safe_queue.hpp"
#include "thread_safe_set.hpp"

namespace lite {

struct event_type_header {
    int kind;  // ACCEPT or CLOSE
};

struct socket_info {
    uint8_t proto;
    uint32_t saddr;
    uint16_t sport;
    uint32_t daddr;
    uint16_t dport;
    uint8_t state;  // TCP connection state
};

struct ConnectionEvent {
    struct event_type_header header;
    struct socket_info socket;
};

struct ParseResult{
    unsigned char* payload;
    int len;
    bool request_dir;
    // connection
};


template <typename Application, typename Request, typename Response,
          typename ConnectionInfo, typename CacheKey, typename CacheEntry>
class EbpfWorker {
  using ConnectionInstance = Connection<Application, Request, Response,
                                        ConnectionInfo, CacheKey, CacheEntry>;
  using LiteCoreInstance = LiteCore<Application, Request, Response,
                                    ConnectionInfo, CacheKey, CacheEntry>;

 public:
  explicit EbpfWorker(LiteCoreInstance &lite_core,
                  std::barrier<std::function<void()>> &barrier);

  ~EbpfWorker();

  /// Create the worker thread and start running the event loop.
  void Run(const char name[] = "lite-worker");


  //Set Mode
  static int SetMode(int mode);

  /// The file descriptor used to signal the worker thread.
  evutil_socket_t notify_event_fd;

  /// The queue used to store the notification.
  ThreadSafeQueue<WorkerMessage> notify_queue_;

  /// The connections managed by the worker thread.
  ThreadSafeSet<ConnectionInstance *> conns_;

  /// Source Address, port to connection map
  std::unordered_map<std::pair<uint32_t, uint16_t>, ConnectionInstance *> source_port_to_conn_;

 private:
  /// PID of the worker thread.
  pthread_t thread_id_;

  /// The event base for the worker thread.
  struct event_base *base_;

  /// The event used to notify the worker thread.
  struct event notify_event_;

  struct litesys_bpf *skel;

  /// The underlying service implementation.
  LiteCoreInstance &lite_core_;

  std::barrier<std::function<void()>> &barrier_;

  /// The entry point for the worker thread.
  static void *ThreadBody(void *arg_self);

  /// Handle a new notification.
  static void NotifyHandler(evutil_socket_t fd, short which, void *arg_self);

  /// Parse the TCP data.
  static struct parse_result parse_tcp_data(unsigned char *buffer, int size);

  /// Check the direction of the TCP data.
  static bool check_dir(char* src_ip, uint16_t sport, char* dst_ip, uint16_t dport);

  /// Convert the port to network order.
  static uint16_t htons_custom(uint16_t i);
};

}  // namespace lite