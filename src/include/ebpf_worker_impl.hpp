#pragma once

#include <event.h>
#include <pthread.h>
#include <sys/eventfd.h>
#include <unistd.h>

#include <cstdlib>
#include <iostream>
#include <syncstream>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <linux/if_packet.h>
#include <linux/if_ether.h>
#include <arpa/inet.h>
#include <bpf/libbpf.h>
#include <net/ethernet.h>
#include <netinet/ip.h>
#include <netinet/tcp.h>
#include <ctype.h>
#include <event2/event.h>
#include <event2/util.h>
#include <bpf/bpf.h>
#include "litesys.skel.h"

#include "ebpf_worker.hpp"

#define SO_ATTACH_BPF 50
#define SO_DETACH_BPF 27
#define BUFFER_SIZE 65536
#define SERVER_PORT 6379
#define ACCEPT 1
#define CLOSE 2

namespace lite {

template <typename Application, typename Request, typename Response,
          typename ConnectionInfo, typename CacheKey, typename CacheEntry>
Worker<Application, Request, Response, ConnectionInfo, CacheKey,
       CacheEntry>::EbpfWorker(LiteCoreInstance &lite_core,
                           std::barrier<std::function<void()>> &barrier)
    : lite_core_(lite_core), barrier_(barrier) {

  struct bpf_program *prog;
  struct ring_buffer *rb;
  int err;

  skel = litesys_bpf__open_and_load();
  if (!skel) {
    fprintf(stderr, "Failed to open and load BPF skeleton\n");
    return;
  }

  err = litesys_bpf__attach(skel);
  if (err) {
    fprintf(stderr, "Failed to attach BPF skeleton\n");
    return;
  }

  notify_event_fd = socket(AF_PACKET, SOCK_RAW, htons_custom(ETH_P_ALL));
  if (notify_event_fd < 0) {
    perror("Socket creation failed");
    return;
  }

  int prog_fd = bpf_program__fd(skel->progs.socket__filter_tcp);

  if (setsockopt(notify_event_fd, SOL_SOCKET, SO_ATTACH_BPF, &prog_fd, sizeof(prog_fd)) < 0) {
    perror("Error attaching BPF program");
    close(notify_event_fd);
    return;
  }
  set_mode(skel, 0);

  rb = ring_buffer__new(bpf_map__fd(skel->maps.conn_ringbuf), handle_event, NULL, NULL);
  if (!rb) {
    printf("Failed to create ring buffer\n");
    return;
  }

  struct event_config *ev_config;
  ev_config = event_config_new();
  event_config_set_flag(ev_config, EVENT_BASE_FLAG_NOLOCK);
  base_ = event_base_new_with_config(ev_config);
  event_config_free(ev_config);
  event_base_priority_init(base_, 2);

  event_set(&notify_event_, notify_event_fd, EV_READ | EV_PERSIST,
            NotifyHandler, this);

  event_base_set(base_, &notify_event_);
  event_priority_set(&notify_event_, 0);  // highest priority

  LOG_IF(FATAL, event_add(&notify_event_, 0) == -1)
      << "Can't monitor libevent notify pipe\n";
}

template <typename Application, typename Request, typename Response,
          typename ConnectionInfo, typename CacheKey, typename CacheEntry>
Worker<Application, Request, Response, ConnectionInfo, CacheKey,
       CacheEntry>::~EbpfWorker() {
  setsockopt(notify_event_fd, SOL_SOCKET, SO_DETACH_BPF, &prog_fd, sizeof(prog_fd));
  close(notify_event_fd);
  socket_bpf__destroy(skel);
  event_del(&notify_event_);
  event_base_free(base_);

  conns_.visit_all([](const auto &conn) { delete conn; });
}

template <typename Application, typename Request, typename Response,
          typename ConnectionInfo, typename CacheKey, typename CacheEntry>
void Worker<Application, Request, Response, ConnectionInfo, CacheKey,
            CacheEntry>::Run(const char name[]) {
  pthread_attr_t attr;

  pthread_attr_init(&attr);

  PCHECK(!pthread_create(&thread_id_, &attr, ThreadBody, this))
      << "Can't create thread: " << name << std::endl;

  pthread_setname_np(thread_id_, name);
  pthread_attr_destroy(&attr);
}

template <typename Application, typename Request, typename Response,
          typename ConnectionInfo, typename CacheKey, typename CacheEntry>
void *Worker<Application, Request, Response, ConnectionInfo, CacheKey,
             CacheEntry>::ThreadBody(void *arg_self) {
  Worker *self = static_cast<Worker *>(arg_self);

  event_base_loop(self->base_, 0);
  event_base_free(self->base_);

  return NULL;
}


template <typename Application, typename Request, typename Response,
          typename ConnectionInfo, typename CacheKey, typename CacheEntry>
void Worker<Application, Request, Response, ConnectionInfo, CacheKey,
            CacheEntry>::NotifyHandler(evutil_socket_t fd, short which,
                                       void *arg_conn) {
  unsigned char buffer[BUFFER_SIZE];
  struct sockaddr_in sender_addr;
  socklen_t addr_len = sizeof(sender_addr);
  ssize_t n = recvfrom(fd, buffer, sizeof(buffer) - 1, 0, (struct sockaddr *)&sender_addr, &addr_len);

  if (n > 0) {
    struct ParseResult result = parse_tcp_data(buffer, n);
    if (result.request_dir){
      //handle request
      // printf("Handle request\n");
      // const auto res = conn->request_->Deserialize(result.payload, result.payload + result.len);
      // if (!conn->lite_core_.HandleRequest(
      //         std::move(conn->request_), conn->extra_app_info_,
      //         conn->pending_requests_, conn->client_fd_, conn->backend_fd_,
      //         &conn->cache_, &conn->logger_, true)) {
      //     delete conn;
      //     return;
      // }
    } else {
      //handle response
      // printf("Handle response\n");
      // const auto res = conn->response_->Deserialize(result.payload, result.payload + result.len);
      // if (!conn->lite_core_.HandleResponse(
      //         std::move(conn->response_), conn->extra_app_info_,
      //         conn->pending_requests_, conn->client_fd_, &conn->cache_,
      //         true)) {
      //     delete conn;
      //     return;
      // }
    }
  } else if (n == 0) {
    printf("Connection closed by peer\n");
    // event_base_loopbreak((struct event_base *)arg); // Break the event loop
  } else {
      perror("recvfrom error");
  }


  // if (fd == self->notify_event_fd) {
  //   uint64_t counter = 0;
  //   if (read(fd, &counter, sizeof(uint64_t)) != sizeof(uint64_t)) {
  //     LOG(ERROR) << "Worker can't read from libevent pipe\n";
  //     return;
  //   }
  //   while (counter--) {
  //     const WorkerMessage msg = self->notify_queue_.pop_front();
  //     if (msg.type == WorkerMessage::Type::kNewClientConnection) {
  //       auto new_connection =
  //           new ConnectionInstance(msg.fd, EV_READ | EV_PERSIST, self->base_,
  //                                  ConnectionInstance::ClientHandler, nullptr,
  //                                  self->lite_core_, true, self);
  //       if (!new_connection) {
  //         LOG(ERROR) << "failed to create listening connection\n";
  //         return;
  //       }
  //       self->lite_core_.live_connections_.insert(new_connection);
  //       self->conns_.insert(new_connection);
  //     } else if (msg.type == WorkerMessage::Type::kBarrier) {
  //       LOG(INFO) << "Thread " << self->thread_id_ << " reaches sync point"
  //                 << std::endl;
  //       self->barrier_.arrive_and_wait();
  //       self->barrier_.arrive_and_wait();
  //       LOG(INFO) << "Thread " << self->thread_id_ << " exits sync point"
  //                 << std::endl;
  //     }
  //   }
  // } else {
  // }
}

template <typename Application, typename Request, typename Response,
          typename ConnectionInfo, typename CacheKey, typename CacheEntry>
struct ParseResult Worker<Application, Request, Response, ConnectionInfo, CacheKey,
            CacheEntry>::parse_tcp_data(unsigned char *buffer, int size) {
    struct ethhdr *eth = (struct ethhdr *)buffer;
    struct ParseResult result = {0};
    if (ntohs(eth->h_proto) != ETH_P_IP)
        return result;

    struct iphdr *ip = (struct iphdr *)(buffer + sizeof(struct ethhdr));
    if (ip->protocol != IPPROTO_TCP)
        return result;

    struct tcphdr *tcp = (struct tcphdr *)(buffer + sizeof(struct ethhdr) + 
                                         (ip->ihl * 4));

    char src_ip[INET_ADDRSTRLEN];
    char dst_ip[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &(ip->saddr), src_ip, INET_ADDRSTRLEN);
    inet_ntop(AF_INET, &(ip->daddr), dst_ip, INET_ADDRSTRLEN);

    int ip_header_len = ip->ihl * 4;   // Length of IP header in bytes
  close(notify_event_fd);
    int tcp_header_len = tcp->doff * 4; // Length of TCP header in bytes
    int headers_total_len = sizeof(struct ethhdr) + ip_header_len + tcp_header_len;

    if (size > headers_total_len) {
        int payload_size = size - headers_total_len;
        unsigned char *payload = buffer + headers_total_len;
        printf("new tcp data, %s:%d -> %s:%d\n", 
           src_ip, ntohs(tcp->source),
           dst_ip, ntohs(tcp->dest));
        result.request_dir = check_dir(src_ip, ntohs(tcp->source), dst_ip, ntohs(tcp->dest));
        result.payload = payload;
        result.len = payload_size;
        fwrite(payload, sizeof(char), payload_size, stdout);
        printf("\n");
        return result;
    } 
    return result;
}

template <typename Application, typename Request, typename Response,
          typename ConnectionInfo, typename CacheKey, typename CacheEntry>
bool Worker<Application, Request, Response, ConnectionInfo, CacheKey,
            CacheEntry>::check_dir(char* src_ip, uint16_t sport, char* dst_ip, uint16_t dport) {
    if (sport == SERVER_PORT) return false;
    else if (dport == SERVER_PORT) return true;
    return false;
}

template <typename Application, typename Request, typename Response,
          typename ConnectionInfo, typename CacheKey, typename CacheEntry>
int Worker<Application, Request, Response, ConnectionInfo, CacheKey,
            CacheEntry>::SetMode(int mode){
  uint32_t key=0;
  uint64_t value;
  value = mode;
  if (bpf_map_update_elem(bpf_map__fd(skel->maps.emergency), &key, &value, BPF_ANY) < 0) {
      std::cerr << "Failed to update mode value in map: " << strerror(errno) << std::endl;
      return -1;
  }
  if (bpf_map_lookup_elem(bpf_map__fd(skel->maps.emergency), &key, &value) < 0) {
      std::cerr << "Failed to look up map mode value: " << strerror(errno) << std::endl;
      return -1;
  }
  return 0;
}

template <typename Application, typename Request, typename Response,
          typename ConnectionInfo, typename CacheKey, typename CacheEntry>
static uint16_t Worker<Application, Request, Response, ConnectionInfo, CacheKey,
            CacheEntry>::htons_custom(uint16_t i) {
    return (i << 8) | (i >> 8);
}

}  // namespace lite