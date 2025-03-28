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
#include <sys/ioctl.h>
#include "litesys.skel.h"

#include "ebpf_worker.hpp"

#define SO_ATTACH_BPF 50
#define BUFFER_SIZE 65536
#define SERVER_PORT 6379
#define ACCEPT 1
#define CLOSE 2

namespace lite {

template <typename Application, typename Request, typename Response,
          typename ConnectionInfo, typename CacheKey, typename CacheEntry>
EbpfWorker<Application, Request, Response, ConnectionInfo, CacheKey,
       CacheEntry>::EbpfWorker(LiteCoreInstance &lite_core,
                           std::barrier<std::function<void()>> &barrier)
    : WorkerBase(lite_core, barrier),  // Call base class constructor
      lite_core_(lite_core), 
      barrier_(barrier) {

  struct bpf_program *prog;
  int err;

  count=0;

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

  int rcvbuf_size = 20 * 1024 * 1024;  // 70MB
  if (setsockopt(notify_event_fd, SOL_SOCKET, SO_RCVBUF, &rcvbuf_size, sizeof(rcvbuf_size)) < 0) {
      perror("Failed to set SO_RCVBUF");
      close(notify_event_fd);
      return;
  }

  // struct tpacket_req req = {
  //     .tp_block_size = 4096,  // Memory block size
  //     .tp_block_nr = 64,      // Number of blocks
  //     .tp_frame_size = 2048,  // Frame size
  //     .tp_frame_nr = 128      // Number of frames
  // };
  // if (setsockopt(notify_event_fd, SOL_PACKET, PACKET_RX_RING, &req, sizeof(req)) < 0) {
  //     perror("Failed to set PACKET_RX_RING");
  //     close(notify_event_fd);
  //     return;
  // }

  prog_fd_ = bpf_program__fd(skel->progs.socket__filter_tcp);

  if (setsockopt(notify_event_fd, SOL_SOCKET, SO_ATTACH_BPF, &prog_fd_, sizeof(prog_fd_)) < 0) {
    perror("Error attaching BPF program");
    close(notify_event_fd);
    return;
  }
  SetMode(0);

  rb = ring_buffer__new(bpf_map__fd(skel->maps.conn_ringbuf), HandleConnection, this, NULL);
  if (!rb) {
    printf("Failed to create ring buffer\n");
    return;
  }

  buffer = (unsigned char *)malloc(BUFFER_SIZE);

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

  // Create and set up timer event
  struct event* timer_event = event_new(base_, -1, EV_PERSIST,
                                      TimerHandler, this);
  struct timeval tv = {0, 100000};  // 100ms interval
  event_add(timer_event, &tv);

  LOG_IF(FATAL, event_add(&notify_event_, 0) == -1)
      << "Can't monitor libevent notify pipe\n";
}

template <typename Application, typename Request, typename Response,
          typename ConnectionInfo, typename CacheKey, typename CacheEntry>
EbpfWorker<Application, Request, Response, ConnectionInfo, CacheKey,
       CacheEntry>::~EbpfWorker() {
  setsockopt(notify_event_fd, SOL_SOCKET, SO_DETACH_BPF, &prog_fd_, sizeof(prog_fd_));
  close(notify_event_fd);
  litesys_bpf__destroy(skel);
  event_del(&notify_event_);
  event_base_free(base_);
  free(buffer);

  conns_.visit_all([](const auto &conn) { delete conn; });
}

template <typename Application, typename Request, typename Response,
          typename ConnectionInfo, typename CacheKey, typename CacheEntry>
void EbpfWorker<Application, Request, Response, ConnectionInfo, CacheKey,
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
void *EbpfWorker<Application, Request, Response, ConnectionInfo, CacheKey,
             CacheEntry>::ThreadBody(void *arg_self) {
  auto *self = static_cast<EbpfWorker<Application, Request, Response, 
                          ConnectionInfo, CacheKey, CacheEntry>*>(arg_self);
  event_base_loop(self->base_, 0);
  event_base_free(self->base_);
  return NULL;
}

template <typename Application, typename Request, typename Response,
          typename ConnectionInfo, typename CacheKey, typename CacheEntry>
void EbpfWorker<Application, Request, Response, ConnectionInfo, CacheKey,
            CacheEntry>::NotifyHandler(evutil_socket_t fd, short which,
                                       void *arg_conn) {
  // printf("Next message\n");
  int dropped;
  socklen_t len = sizeof(dropped);
  if (getsockopt(fd, SOL_SOCKET, SO_RXQ_OVFL, &dropped, &len) == 0) {
    std::cout << "Received queue overflowed packets: " << dropped << std::endl;
  }
  auto *self = static_cast<EbpfWorker<Application, Request, Response,
                          ConnectionInfo, CacheKey, CacheEntry>*>(arg_conn);

  int bytes_available;
  if (ioctl(fd, FIONREAD, &bytes_available) < 0) {
      perror("ioctl FIONREAD failed");
      return;
  }
  // printf("Bytes available: %d\n", bytes_available);
  self->count++;
  struct sockaddr_in sender_addr;
  socklen_t addr_len = sizeof(sender_addr);
  ssize_t n = recvfrom(fd, self->buffer, BUFFER_SIZE - 1, 0, (struct sockaddr *)&sender_addr, &addr_len);
  
  
  if (n > 0) {
    ParseResult result = parse_tcp_data(self->buffer, n);
    if (result.request_dir){
      // check if the connection exists
      std::string src_str(result.src_ip.get(), INET_ADDRSTRLEN);
      if(self->source_to_conn_.find(std::make_pair(self->ipToUint32(src_str), result.src_port)) == self->source_to_conn_.end()){
        // Add the connection to the connection map
        //print src_str and result.src_port
        // printf("Adding connection to the connection map during request update: %s:%u\n", src_str.c_str(), result.src_port);
        auto new_connection =
            new ConnectionInstance(0,                    // socket fd
                                  0,                      // event flags
                                  self->base_,           // event base
                                  ConnectionInstance::ClientHandler,
                                  nullptr,               // argument for handler
                                  self->lite_core_,      // lite core instance
                                  false,                 // is_server
                                  self);                 // worker instance
        self->source_to_conn_[std::make_pair(self->ipToUint32(src_str), result.src_port)] = new_connection;
        self->lite_core_.live_connections_.insert(new_connection);
        self->conns_.insert(new_connection);
      }
      // print payload here
      self->source_to_conn_[std::make_pair(self->ipToUint32(src_str), result.src_port)]->RequestUpdate(result.payload.get(), result.len);
    } else {
      std::string dst_str(result.dst_ip.get(), INET_ADDRSTRLEN);
      if(self->source_to_conn_.find(std::make_pair(self->ipToUint32(dst_str), result.dst_port)) == self->source_to_conn_.end()){
        //error
        return;
      }
      self->source_to_conn_[std::make_pair(self->ipToUint32(dst_str), result.dst_port)]->ResponseUpdate(result.payload.get(), result.len);
    }
  } else if (n == 0) {
    printf("Connection closed by peer\n");
    // event_base_loopbreak((struct event_base *)arg); // Break the event loop
  } else {
      perror("recvfrom error");
  }
}

template <typename Application, typename Request, typename Response,
          typename ConnectionInfo, typename CacheKey, typename CacheEntry>
int EbpfWorker<Application, Request, Response, ConnectionInfo, CacheKey,
            CacheEntry>::HandleConnection(void *ctx, void *data, size_t data_sz){
    auto *self = static_cast<EbpfWorker<Application, Request, Response, 
                          ConnectionInfo, CacheKey, CacheEntry>*>(ctx);
    struct ConnectionEvent *event = (struct ConnectionEvent *)data;
    char ip_str[INET_ADDRSTRLEN];
    uint16_t port;
    if(event->header.kind == ACCEPT){
        if (!inet_ntop(AF_INET, &event->connection.saddr, ip_str, sizeof(ip_str))) {
            perror("inet_ntop");
            return -1;
        }
        std::string src_str(ip_str);
        if (!inet_ntop(AF_INET, &event->connection.daddr, ip_str, sizeof(ip_str))) {
            perror("inet_ntop");
            return -1;
        }
        std::string dst_str(ip_str);
        if(self->source_to_conn_.find(std::make_pair(self->ipToUint32(dst_str), event->connection.dport)) == self->source_to_conn_.end()){
          //print dst_str and event->connection.dport
          // printf("Adding connection to the connection map during accept update: %s:%u\n", dst_str.c_str(), event->connection.dport);
          auto new_connection =
            new ConnectionInstance(0,                    // socket fd
                                  0,                      // event flags
                                  self->base_,           // event base
                                  ConnectionInstance::ClientHandler,
                                  nullptr,               // argument for handler
                                  self->lite_core_,      // lite core instance
                                  false,                 // is_server
                                  self);                 // worker instance
        self->source_to_conn_[std::make_pair(self->ipToUint32(dst_str), event->connection.dport)] = new_connection;
        self->lite_core_.live_connections_.insert(new_connection);
        self->conns_.insert(new_connection);
        }
    } else{
        if (!inet_ntop(AF_INET, &event->connection.saddr, ip_str, sizeof(ip_str))) {
            perror("inet_ntop");
            return -1;
        }
        std::string src_str(ip_str);
        if (!inet_ntop(AF_INET, &event->connection.daddr, ip_str, sizeof(ip_str))) {
            perror("inet_ntop");
            return -1;
        }
        std::string dest_str(ip_str);
        if(self->source_to_conn_.find(std::make_pair(self->ipToUint32(dest_str), event->connection.dport)) != self->source_to_conn_.end()){
          //print dest_str and event->connection.dport
          // printf("Removing connection from the connection map: %s:%u\n", dest_str.c_str(), event->connection.dport);
          auto connection = self->source_to_conn_[std::make_pair(self->ipToUint32(dest_str), event->connection.dport)];
          self->source_to_conn_[std::make_pair(self->ipToUint32(dest_str), event->connection.dport)] = nullptr;
          // self->lite_core_.live_connections_.erase(new_connection);
          // self->conns_.erase(new_connection);
          delete connection;
        }
    }
    return 0;

}

template <typename Application, typename Request, typename Response,
          typename ConnectionInfo, typename CacheKey, typename CacheEntry>
void EbpfWorker<Application, Request, Response, ConnectionInfo, CacheKey,
            CacheEntry>::TimerHandler(evutil_socket_t fd, short events, void* arg) {
    auto* self = static_cast<EbpfWorker<Application, Request, Response,
                            ConnectionInfo, CacheKey, CacheEntry>*>(arg);
    ring_buffer__poll(self->rb, 100);  // Poll with 100ms timeout
}

template <typename Application, typename Request, typename Response,
          typename ConnectionInfo, typename CacheKey, typename CacheEntry>
ParseResult EbpfWorker<Application, Request, Response, ConnectionInfo, CacheKey,
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

    std::unique_ptr<char[]> src_ip = std::make_unique<char[]>(INET_ADDRSTRLEN);
    std::unique_ptr<char[]> dst_ip = std::make_unique<char[]>(INET_ADDRSTRLEN);
    inet_ntop(AF_INET, &(ip->saddr), src_ip.get(), INET_ADDRSTRLEN);
    inet_ntop(AF_INET, &(ip->daddr), dst_ip.get(), INET_ADDRSTRLEN);

    int ip_header_len = ip->ihl * 4;   // Length of IP header in bytes
    int tcp_header_len = tcp->doff * 4; // Length of TCP header in bytes
    int headers_total_len = sizeof(struct ethhdr) + ip_header_len + tcp_header_len;
    std::unique_ptr<uint8_t[]> payload = std::make_unique<uint8_t[]>(size - headers_total_len);

    if (size > headers_total_len) {
        int payload_size = size - headers_total_len;
        memcpy(payload.get(), buffer + headers_total_len, payload_size);
        // printf("new tcp data, %s:%d -> %s:%d\n", 
        //    src_ip.get(), ntohs(tcp->source),
        //    dst_ip.get(), ntohs(tcp->dest));
        result.request_dir = check_dir(src_ip.get(), ntohs(tcp->source), dst_ip.get(), ntohs(tcp->dest));
        result.src_ip = std::move(src_ip);
        result.src_port = ntohs(tcp->source);
        result.dst_ip=std::move(dst_ip);
        result.dst_port = ntohs(tcp->dest);
        result.payload = std::move(payload);
        result.len = payload_size;
        // fwrite(payload, sizeof(char), payload_size, stdout);
        return result;
    } 
    return result;
}

template <typename Application, typename Request, typename Response,
          typename ConnectionInfo, typename CacheKey, typename CacheEntry>
bool EbpfWorker<Application, Request, Response, ConnectionInfo, CacheKey,
            CacheEntry>::check_dir(char* src_ip, uint16_t sport, char* dst_ip, uint16_t dport) {
    //Incorporate source ip checking
    if (sport == SERVER_PORT) return false;
    else if (dport == SERVER_PORT) return true;
    return false;
}

template <typename Application, typename Request, typename Response,
          typename ConnectionInfo, typename CacheKey, typename CacheEntry>
int EbpfWorker<Application, Request, Response, ConnectionInfo, CacheKey,
            CacheEntry>::SetMode(int mode) {
  uint32_t key=0;
  uint64_t value;
  value = mode;
  if (bpf_map_update_elem(bpf_map__fd(skel->maps.mode), &key, &value, BPF_ANY) < 0) {
      std::cerr << "Failed to update mode value in map: " << strerror(errno) << std::endl;
      return -1;
  }
  if (bpf_map_lookup_elem(bpf_map__fd(skel->maps.mode), &key, &value) < 0) {
      std::cerr << "Failed to look up map mode value: " << strerror(errno) << std::endl;
      return -1;
  }
  return 0;
}

template <typename Application, typename Request, typename Response,
          typename ConnectionInfo, typename CacheKey, typename CacheEntry>
uint16_t EbpfWorker<Application, Request, Response, ConnectionInfo, CacheKey,
            CacheEntry>::htons_custom(uint16_t i) {
    return (i << 8) | (i >> 8);
}

template <typename Application, typename Request, typename Response,
          typename ConnectionInfo, typename CacheKey, typename CacheEntry>
uint32_t EbpfWorker<Application, Request, Response, ConnectionInfo, CacheKey,
            CacheEntry>::ipToUint32(const std::string& ip) {
    struct in_addr addr;
    if (inet_pton(AF_INET, ip.c_str(), &addr) == 1) {
        return ntohl(addr.s_addr);  // Convert from network byte order to host byte order
    } else {
        throw std::invalid_argument("Invalid IP address format");
    }
}

}  // namespace lite