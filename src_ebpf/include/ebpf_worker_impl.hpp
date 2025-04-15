#pragma once

#include <arpa/inet.h>
#include <bpf/bpf.h>
#include <bpf/libbpf.h>
#include <ctype.h>
#include <event.h>
#include <event2/event.h>
#include <event2/util.h>
#include <linux/if_ether.h>
#include <linux/if_packet.h>
#include <net/ethernet.h>
#include <netinet/ip.h>
#include <netinet/tcp.h>
#include <pthread.h>
#include <string.h>
#include <sys/eventfd.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#include <cstdlib>
#include <iostream>
#include <syncstream>

#include "ebpf_worker.hpp"
#include "litesys.skel.h"

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
    : WorkerBase(lite_core, barrier) {
  // notify_event_fd = socket(AF_PACKET, SOCK_RAW, htons_custom(ETH_P_ALL));
  // if (notify_event_fd < 0) {
  //   perror("Socket creation failed");
  //   return;
  // }

  // int rcvbuf_size = 20 * 1024 * 1024;  // 70MB
  // if (setsockopt(notify_event_fd, SOL_SOCKET, SO_RCVBUF, &rcvbuf_size,
  // sizeof(rcvbuf_size)) < 0) {
  //     perror("Failed to set SO_RCVBUF");
  //     close(notify_event_fd);
  //     return;
  // }

  buffer = (unsigned char *)malloc(MAX_MSG_SIZE);

  // event_set(&notify_event_, notify_event_fd, EV_READ | EV_PERSIST,
  //           NotifyHandler, this);

  // event_base_set(base_, &notify_event_);
  // event_priority_set(&notify_event_, 0);  // highest priority

  // Create and set up timer event
  timer_event = event_new(this->base_, -1, EV_PERSIST, TimerHandler, this);
  struct timeval tv = {0, 100000};  // 100ms interval
  event_add(timer_event, &tv);

  // LOG_IF(FATAL, event_add(&notify_event_, 0) == -1)
  //     << "Can't monitor libevent notify pipe\n";
}

template <typename Application, typename Request, typename Response,
          typename ConnectionInfo, typename CacheKey, typename CacheEntry>
EbpfWorker<Application, Request, Response, ConnectionInfo, CacheKey,
           CacheEntry>::~EbpfWorker() {
  if (pb) ring_buffer__free(pb);
  if (rb) ring_buffer__free(rb);
  // setsockopt(notify_event_fd, SOL_SOCKET, SO_DETACH_BPF, &prog_fd_,
  // sizeof(prog_fd_)); close(notify_event_fd);
  if (skel) litesys_bpf__destroy(skel);
  if (redis_request_prog_fd)
    bpf_prog_detach2(redis_request_prog_fd, sock_map_fd,
                     BPF_SK_SKB_STREAM_PARSER);
  if (redis_response_prog_fd)
    bpf_prog_detach2(redis_response_prog_fd, sock_map_fd,
                     BPF_SK_SKB_STREAM_VERDICT);
  if (sockops_monitor_fd)
    bpf_prog_detach2(sockops_monitor_fd, cg_fd, BPF_CGROUP_SOCK_OPS);
  if (sock_map_fd) close(sock_map_fd);
  if (cg_fd) close(cg_fd);
  if (timer_event) event_del(timer_event);
  free(buffer);
}

template <typename Application, typename Request, typename Response,
          typename ConnectionInfo, typename CacheKey, typename CacheEntry>
int EbpfWorker<Application, Request, Response, ConnectionInfo, CacheKey,
               CacheEntry>::get_socket_fd_from_pid(int pid, int target_fd) {
  int pidfd = syscall(SYS_pidfd_open, pid, 0);
  if (pidfd < 0) {
    perror("pidfd_open failed");
    return -1;
  }

  int sockfd = syscall(SYS_pidfd_getfd, pidfd, target_fd, 0);
  if (sockfd < 0) {
    perror("pidfd_getfd failed");
    close(pidfd);
    return -1;
  }
  // printf("Socket fd: %d\n", sockfd);

  close(pidfd);
  return sockfd;
}

template <typename Application, typename Request, typename Response,
          typename ConnectionInfo, typename CacheKey, typename CacheEntry>
std::string EbpfWorker<Application, Request, Response, ConnectionInfo, CacheKey,
                       CacheEntry>::executeCommand(const std::string &command) {
  std::array<char, 128> buffer;
  std::string result;
  std::unique_ptr<FILE, decltype(&pclose)> pipe(popen(command.c_str(), "r"),
                                                pclose);

  if (!pipe) {
    std::cerr << "Failed to run command: " << command << std::endl;
    return "";
  }

  while (fgets(buffer.data(), buffer.size(), pipe.get()) != nullptr) {
    result += buffer.data();
  }
  return result;
}

template <typename Application, typename Request, typename Response,
          typename ConnectionInfo, typename CacheKey, typename CacheEntry>
socket_info EbpfWorker<Application, Request, Response, ConnectionInfo, CacheKey,
                       CacheEntry>::findSocketFD(int port) {
  socket_info info = {0, 0, 0};
  std::ostringstream command;
  command << "sudo lsof -i :" << port;

  std::string output = executeCommand(command.str());
  if (output.empty()) {
    std::cerr << "No output from lsof. Ensure the port is in use and you have "
                 "root access."
              << std::endl;
    return info;
  }

  std::istringstream stream(output);
  std::string line;
  std::getline(stream, line);  // Skip header

  std::regex pattern(R"(\S+\s+(\d+)\s+\S+\s+(\d+)u\s+)");
  bool found = false;

  while (std::getline(stream, line)) {
    std::smatch match;
    if (std::regex_search(line, match, pattern)) {
      std::string pid = match[1];
      std::string fd = match[2];

      std::cout << "Found socket: PID = " << pid << ", FD = " << fd
                << std::endl;
      found = true;
      uint64_t socket_fd = std::stoull(fd);
      uint64_t pid_int = std::stoull(pid);
      uint64_t ref_socket_fd = get_socket_fd_from_pid(pid_int, socket_fd);
      info = {socket_fd, pid_int, ref_socket_fd};
      return info;
    }
  }

  if (!found) {
    std::cerr << "No matching socket found on port " << port << std::endl;
  }
  return info;
}

template <typename Application, typename Request, typename Response,
          typename ConnectionInfo, typename CacheKey, typename CacheEntry>
int EbpfWorker<Application, Request, Response, ConnectionInfo, CacheKey,
               CacheEntry>::Run(const char name[]) {
  socket_info info = findSocketFD(SERVER_PORT);
  while (info.socket_fd == 0) {
    sleep(1);
    LOG(INFO) << "Waiting for socket to be created" << std::endl;
    info = findSocketFD(SERVER_PORT);
  }

  LOG(INFO) << "Redis- pid: " << info.pid << " socket_fd: " << info.socket_fd
            << " ref_socket_fd: " << info.ref_socket_fd << std::endl;

  // Remove socket creation and setup code
  skel = litesys_bpf__open_and_load();
  if (!skel) {
    fprintf(stderr, "Failed to open and load BPF skeleton\n");
    return -1;
  }

  cg_fd = open("/sys/fs/cgroup", O_DIRECTORY | O_RDONLY);
  sockops_monitor_fd = bpf_program__fd(skel->progs.bpf_sockops_monitor);
  bpf_prog_attach(sockops_monitor_fd, cg_fd, BPF_CGROUP_SOCK_OPS, 0);

  sock_map_fd = bpf_map__fd(skel->maps.sock_hash);

  //   redis_request_prog_fd =
  //   bpf_program__fd(skel->progs.handle_redis_request); if
  //   (bpf_prog_attach(redis_request_prog_fd, sock_map_fd,
  //   BPF_SK_SKB_STREAM_PARSER, 0) != 0) {
  //       perror("bpf_prog_attach failed for redis_request_prog_fd");
  //       return 1;
  //   }
  redis_response_prog_fd =
      bpf_program__fd(skel->progs.handle_redis_response_sk_msg);
  if (bpf_prog_attach(redis_response_prog_fd, sock_map_fd, BPF_SK_MSG_VERDICT,
                      0) != 0) {
    perror("bpf_prog_attach failed for redis_response_prog_fd");
    return 1;
  }

  redis_request_prog_fd = bpf_program__fd(skel->progs.handle_redis_request);
  if (bpf_prog_attach(redis_request_prog_fd, sock_map_fd,
                      BPF_SK_SKB_STREAM_VERDICT, 0) != 0) {
    perror("bpf_prog_attach failed for redis_request_prog_fd");
    return 1;
  }

  auto err = litesys_bpf__attach(skel);
  if (err) {
    fprintf(stderr, "Failed to attach BPF skeleton\n");
    return -1;
  }

  SetEmergencyMode(false);

  pb = ring_buffer__new(bpf_map__fd(skel->maps.msgs_ringbuf), HandlePacket,
                        this, NULL);
  if (!pb) {
    printf("Failed to create perf buffer\n");
    return -1;
  }

  rb = ring_buffer__new(bpf_map__fd(skel->maps.conn_ringbuf), HandleConnection,
                        this, NULL);
  if (!rb) {
    printf("Failed to create perf buffer\n");
    return -1;
  }

  pthread_attr_t attr;
  pthread_attr_init(&attr);
  PCHECK(!pthread_create(&this->thread_id_, &attr, ThreadBody, this))
      << "Can't create thread: " << name << std::endl;

  pthread_setname_np(this->thread_id_, name);
  pthread_attr_destroy(&attr);
  return info.ref_socket_fd;
}

template <typename Application, typename Request, typename Response,
          typename ConnectionInfo, typename CacheKey, typename CacheEntry>
void *EbpfWorker<Application, Request, Response, ConnectionInfo, CacheKey,
                 CacheEntry>::ThreadBody(void *arg_self) {
  auto *self =
      static_cast<EbpfWorker<Application, Request, Response, ConnectionInfo,
                             CacheKey, CacheEntry> *>(arg_self);
  while (true) {
    self->cnt_event_processed_last_poll = 0;
    self->cnt_event_processed_last_poll += ring_buffer__poll(self->rb, 1);
    self->cnt_event_processed_last_poll += ring_buffer__poll(self->pb, 10);
    if (self->cnt_event_processed_last_poll == 0) {
      self->consecutive_empty_poll++;
    } else {
      self->consecutive_empty_poll = 0;
    }
  }
  event_base_loop(self->base_, 0);
  event_base_free(self->base_);
  return NULL;
}

template <typename Application, typename Request, typename Response,
          typename ConnectionInfo, typename CacheKey, typename CacheEntry>
int EbpfWorker<Application, Request, Response, ConnectionInfo, CacheKey,
               CacheEntry>::ClearAllInFlightTraffic() {
  consecutive_empty_poll = 0;
  while (consecutive_empty_poll < 10) {
    __asm__ volatile("pause");
  }
  return 0;
}

template <typename Application, typename Request, typename Response,
          typename ConnectionInfo, typename CacheKey, typename CacheEntry>
int EbpfWorker<Application, Request, Response, ConnectionInfo, CacheKey,
               CacheEntry>::HandlePacket(void *ctx, void *data,
                                         size_t data_sz) {
  auto *self =
      static_cast<EbpfWorker<Application, Request, Response, ConnectionInfo,
                             CacheKey, CacheEntry> *>(ctx);
  struct socket_data_event_t *event = (struct socket_data_event_t *)data;
  bool is_request =
      event->is_read;  // true for read (request), false for write (response)
  // Create a local buffer for the message data
  size_t msg_size = strlen(event->msg);
  memcpy(self->buffer, event->msg, msg_size);
  self->buffer[msg_size] = '\0';  // Ensure null termination
  auto remote_addr = ntohl(event->remote_addr);
  auto remote_port = event->remote_port;  // already changed endian in eBPF

  if (is_request) {
    // std::cout << "Received request: " << self->buffer << std::endl;
    // Check if the connection exists
    if (self->source_to_conn_.find(std::make_pair(remote_addr, remote_port)) ==
        self->source_to_conn_.end()) {
      // Add the connection to the connection map
      char ip_str[INET_ADDRSTRLEN];
      if (!inet_ntop(AF_INET, &event->remote_addr, ip_str, sizeof(ip_str))) {
        perror("inet_ntop");
        return -1;
      }
      std::string src_str(ip_str);
      LOG(INFO) << "Connection not found for request, creating new connection: "
                << src_str << ":" << remote_port << std::endl;
      auto new_connection =
          new ConnectionInstance(0,            // socket fd
                                 0,            // event flags
                                 self->base_,  // event base
                                 ConnectionInstance::ClientHandler,
                                 nullptr,           // argument for handler
                                 self->lite_core_,  // lite core instance
                                 false,             // is_server
                                 self);             // worker instance
      self->source_to_conn_[std::make_pair(remote_addr, remote_port)] =
          new_connection;
      self->lite_core_.live_connections_.insert(new_connection);
      self->conns_.insert(new_connection);
    }

    // LOG(INFO) << "Update request: "
    //           << self->source_to_conn_[std::make_pair(remote_addr, remote_port)]
    //           << " " << remote_addr << ":" << remote_port;

    // Update the connection with request data
    self->source_to_conn_[std::make_pair(remote_addr, remote_port)]
        ->RequestUpdate(
            self->buffer, event->msg_size,
            event->seq_num);  // Using 0 for seq_num as it's not in packet_data
  } else {
    // std::cout << "Received response: " << self->buffer << std::endl;
    if (self->source_to_conn_.find(std::make_pair(remote_addr, remote_port)) ==
        self->source_to_conn_.end()) {
      // Connection not found for response
      LOG(INFO) << "Connection not found for response: " << remote_addr << ":"
                << remote_port << std::endl;
      return 0;
    }

    // LOG(INFO) << "Update response: "
    //           << self->source_to_conn_[std::make_pair(remote_addr, remote_port)]
    //           << " " << remote_addr << ":" << remote_port;

    // Update the connection with response data
    self->source_to_conn_[std::make_pair(remote_addr, remote_port)]
        ->ResponseUpdate(
            self->buffer, event->msg_size,
            event->seq_num);  // Using 0 for seq_num as it's not in packet_data
  }
  return 0;
}

template <typename Application, typename Request, typename Response,
          typename ConnectionInfo, typename CacheKey, typename CacheEntry>
int EbpfWorker<Application, Request, Response, ConnectionInfo, CacheKey,
               CacheEntry>::HandleConnection(void *ctx, void *data,
                                             size_t data_sz) {
  auto *self =
      static_cast<EbpfWorker<Application, Request, Response, ConnectionInfo,
                             CacheKey, CacheEntry> *>(ctx);
  struct ConnectionEvent *event = (struct ConnectionEvent *)data;
  char ip_str[INET_ADDRSTRLEN];
  uint16_t dport =
      event->connection.dport;  // cannot bind packed field to uint16_t &
  if (event->header.kind == ACCEPT) {
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
    if (self->source_to_conn_.find(std::make_pair(
            self->ipToUint32(dst_str), dport)) == self->source_to_conn_.end()) {
      // print dst_str and event->connection.dport
      //   LOG(INFO) << "Adding connection to the connection map during accept
      //   update: " << dst_str << ":" << event->connection.dport << std::endl;
      //   return 0;
      auto new_connection =
          new ConnectionInstance(0,            // socket fd
                                 0,            // event flags
                                 self->base_,  // event base
                                 ConnectionInstance::ClientHandler,
                                 nullptr,           // argument for handler
                                 self->lite_core_,  // lite core instance
                                 false,             // is_server
                                 self);             // worker instance
      self->source_to_conn_[std::make_pair(self->ipToUint32(dst_str), dport)] =
          new_connection;
      // LOG(INFO) << "New connection: " << self->ipToUint32(dst_str) << ":"
      //           << dport << " old daddr: " << event->connection.daddr
      //           << std::endl;
      self->lite_core_.live_connections_.insert(new_connection);
      self->conns_.insert(new_connection);
    } else {
      LOG(INFO) << "Connection already exists in the connection map during "
                   "accept update: "
                << dst_str << ":" << event->connection.dport << std::endl;
    }
  } else {
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
    if (self->source_to_conn_.find(
            std::make_pair(self->ipToUint32(dest_str), dport)) !=
        self->source_to_conn_.end()) {
      // print dest_str and event->connection.dport
      printf("Removing connection from the connection map: %s:%u\n",
             dest_str.c_str(), dport);
      return 0;
      auto connection = self->source_to_conn_[std::make_pair(
          self->ipToUint32(dest_str), dport)];
      self->source_to_conn_[std::make_pair(self->ipToUint32(dest_str), dport)] =
          nullptr;
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
                CacheEntry>::TimerHandler(evutil_socket_t fd, short events,
                                          void *arg) {
  auto *self =
      static_cast<EbpfWorker<Application, Request, Response, ConnectionInfo,
                             CacheKey, CacheEntry> *>(arg);
  ring_buffer__poll(self->rb, 100);  // Poll with 100ms timeout
}

template <typename Application, typename Request, typename Response,
          typename ConnectionInfo, typename CacheKey, typename CacheEntry>
int EbpfWorker<Application, Request, Response, ConnectionInfo, CacheKey,
               CacheEntry>::SetEmergencyMode(bool mode) {
  uint32_t key = 0;
  uint32_t value;
  value = mode;
  if (bpf_map_update_elem(bpf_map__fd(skel->maps.emergency_mode), &key, &value,
                          BPF_ANY) < 0) {
    std::cerr << "Failed to update mode value in map: " << strerror(errno)
              << std::endl;
    return -1;
  }
  if (bpf_map_lookup_elem(bpf_map__fd(skel->maps.emergency_mode), &key,
                          &value) < 0) {
    std::cerr << "Failed to look up map mode value: " << strerror(errno)
              << std::endl;
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
                    CacheEntry>::ipToUint32(const std::string &ip) {
  struct in_addr addr;
  if (inet_pton(AF_INET, ip.c_str(), &addr) == 1) {
    return ntohl(
        addr.s_addr);  // Convert from network byte order to host byte order
  } else {
    throw std::invalid_argument("Invalid IP address format");
  }
}

}  // namespace lite