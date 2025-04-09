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


  count=0;

  
  // notify_event_fd = socket(AF_PACKET, SOCK_RAW, htons_custom(ETH_P_ALL));
  // if (notify_event_fd < 0) {
  //   perror("Socket creation failed");
  //   return;
  // }

  // int rcvbuf_size = 20 * 1024 * 1024;  // 70MB
  // if (setsockopt(notify_event_fd, SOL_SOCKET, SO_RCVBUF, &rcvbuf_size, sizeof(rcvbuf_size)) < 0) {
  //     perror("Failed to set SO_RCVBUF");
  //     close(notify_event_fd);
  //     return;
  // }

  

  buffer = (unsigned char *)malloc(MAX_MSG_SIZE);

  struct event_config *ev_config;
  ev_config = event_config_new();
  event_config_set_flag(ev_config, EVENT_BASE_FLAG_NOLOCK);
  base_ = event_base_new_with_config(ev_config);
  event_config_free(ev_config);
  event_base_priority_init(base_, 2);

  // event_set(&notify_event_, notify_event_fd, EV_READ | EV_PERSIST,
  //           NotifyHandler, this);

  // event_base_set(base_, &notify_event_);
  // event_priority_set(&notify_event_, 0);  // highest priority

  // Create and set up timer event
  struct event* timer_event = event_new(base_, -1, EV_PERSIST,
                                      TimerHandler, this);
  struct timeval tv = {0, 100000};  // 100ms interval
  event_add(timer_event, &tv);

  // LOG_IF(FATAL, event_add(&notify_event_, 0) == -1)
  //     << "Can't monitor libevent notify pipe\n";
}

template <typename Application, typename Request, typename Response,
          typename ConnectionInfo, typename CacheKey, typename CacheEntry>
EbpfWorker<Application, Request, Response, ConnectionInfo, CacheKey,
       CacheEntry>::~EbpfWorker() {
  // setsockopt(notify_event_fd, SOL_SOCKET, SO_DETACH_BPF, &prog_fd_, sizeof(prog_fd_));
  // close(notify_event_fd);
  litesys_bpf__destroy(skel);
  // event_del(&notify_event_);
  event_base_free(base_);
  free(buffer);

  conns_.visit_all([](const auto &conn) { delete conn; });
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
                CacheEntry>::executeCommand(const std::string& command) {
    std::array<char, 128> buffer;
    std::string result;
    std::unique_ptr<FILE, decltype(&pclose)> pipe(popen(command.c_str(), "r"), pclose);
    
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
uint64_t EbpfWorker<Application, Request, Response, ConnectionInfo, CacheKey,
                CacheEntry>::findSocketFD(int port) {
    std::ostringstream command;
    command << "sudo lsof -i :" << port;

    std::string output = executeCommand(command.str());
    if (output.empty()) {
        std::cerr << "No output from lsof. Ensure the port is in use and you have root access." << std::endl;
        return 0;
    }

    std::istringstream stream(output);
    std::string line;
    std::getline(stream, line); // Skip header

    std::regex pattern(R"(\S+\s+(\d+)\s+\S+\s+(\d+)u\s+)");
    bool found = false;

    while (std::getline(stream, line)) {
        std::smatch match;
        if (std::regex_search(line, match, pattern)) {
            std::string pid = match[1];
            std::string fd = match[2];

            std::cout << "Found socket: PID = " << pid << ", FD = " << fd << std::endl;
            found = true;
            uint64_t socket_fd = std::stoull(fd);
            uint64_t pid_int = std::stoull(pid);
            uint64_t ref_socket_fd = get_socket_fd_from_pid(pid_int, socket_fd);
            main_pid_ = pid_int;
            main_socket_fd_ = socket_fd;
            return ref_socket_fd;
        }
    }

    if (!found) {
        std::cerr << "No matching socket found on port " << port << std::endl;
    }
    return 0;
}

template <typename Application, typename Request, typename Response,
          typename ConnectionInfo, typename CacheKey, typename CacheEntry>
void EbpfWorker<Application, Request, Response, ConnectionInfo, CacheKey,
            CacheEntry>::Run(const char name[]) {
            
  uint64_t info = findSocketFD(SERVER_PORT);
  while(info == 0){
    sleep(1);
    LOG(INFO) << "Waiting for socket to be created" << std::endl;
    info = findSocketFD(SERVER_PORT);
  }

  LOG(INFO) << "Redis- pid: " << main_pid_ << " socket_fd: " << main_socket_fd_  << " ref_socket_fd: " << info << std::endl;

  // Remove socket creation and setup code
  skel = litesys_bpf__open_and_load();
  if (!skel) {
    fprintf(stderr, "Failed to open and load BPF skeleton\n");
    return;
  }

  auto err = litesys_bpf__attach(skel);
  if (err) {
    fprintf(stderr, "Failed to attach BPF skeleton\n");
    return;
  }


  SetMode(0);

  SetSocketInfo(main_socket_fd_, main_pid_);

  pb = ring_buffer__new(bpf_map__fd(skel->maps.msgs_ringbuf), HandlePacket, this, NULL);
  if (!pb) {
    printf("Failed to create perf buffer\n");
    return;
  }

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
  while(true){
    // ring_buffer__poll(self->rb, 100);
    ring_buffer__poll(self->pb, 100000000);
  }
  event_base_loop(self->base_, 0);
  event_base_free(self->base_);
  return NULL;
}

template <typename Application, typename Request, typename Response,
          typename ConnectionInfo, typename CacheKey, typename CacheEntry>
int EbpfWorker<Application, Request, Response, ConnectionInfo, CacheKey,
            CacheEntry>::HandlePacket(void *ctx, void *data, size_t data_sz) {
    auto *self = static_cast<EbpfWorker<Application, Request, Response,
                          ConnectionInfo, CacheKey, CacheEntry>*>(ctx);
    struct socket_data_event_t *event = (struct socket_data_event_t *)data;
    bool is_request = event->is_read;  
    size_t msg_size = strlen(event->msg);
    memcpy(self->buffer, event->msg, msg_size);
    self->buffer[msg_size] = '\0'; 
    if(event->is_connection){
        if (self->source_to_conn_.find(event->fd) 
            == self->source_to_conn_.end()) {
            auto new_connection =
                new ConnectionInstance(0,                    // socket fd
                                    0,                      // event flags
                                    self->base_,           // event base
                                    ConnectionInstance::ClientHandler,
                                    nullptr,               // argument for handler
                                    self->lite_core_,      // lite core instance
                                    false,                 // is_server
                                    self);                 // worker instance
            self->source_to_conn_[event->fd] = new_connection;
            self->lite_core_.live_connections_.insert(new_connection);
            self->conns_.insert(new_connection);
        }
        return 0;
    }
    if (is_request) {
        // std::cout << "Received request: " << self->buffer << std::endl;
        if (self->source_to_conn_.find(event->fd) 
            == self->source_to_conn_.end()) {
            auto new_connection =
                new ConnectionInstance(0,                    // socket fd
                                    0,                      // event flags
                                    self->base_,           // event base
                                    ConnectionInstance::ClientHandler,
                                    nullptr,               // argument for handler
                                    self->lite_core_,      // lite core instance
                                    false,                 // is_server
                                    self);                 // worker instance
            self->source_to_conn_[event->fd] = new_connection;
            self->lite_core_.live_connections_.insert(new_connection);
            self->conns_.insert(new_connection);
        }
        
        // Update the connection with request data
        self->source_to_conn_[event->fd]
            ->RequestUpdate(self->buffer, event->msg_size, 1);  // Using 0 for seq_num as it's not in packet_data
    } else {
        // std::cout << "Received response: " << self->buffer << std::endl;

        if (self->source_to_conn_.find(event->fd) 
            == self->source_to_conn_.end()) {
            printf("Connection not found for response: %ld\n", event->fd);
            return 0;
        }
        self->source_to_conn_[event->fd]
            ->ResponseUpdate(self->buffer, event->msg_size, 1);  // Using 0 for seq_num as it's not in packet_data
    }
    return 0;
}

template <typename Application, typename Request, typename Response,
          typename ConnectionInfo, typename CacheKey, typename CacheEntry>
int EbpfWorker<Application, Request, Response, ConnectionInfo, CacheKey,
            CacheEntry>::HandleConnection(void *ctx, void *data, size_t data_sz){
    auto *self = static_cast<EbpfWorker<Application, Request, Response, 
                          ConnectionInfo, CacheKey, CacheEntry>*>(ctx);
    struct ConnectionEvent *event = (struct ConnectionEvent *)data;
    char ip_str[INET_ADDRSTRLEN];
    uint16_t dport = event->connection.dport; // cannot bind packed field to uint16_t &
    // if(event->header.kind == ACCEPT){
    //     if (!inet_ntop(AF_INET, &event->connection.saddr, ip_str, sizeof(ip_str))) {
    //         perror("inet_ntop");
    //         return -1;
    //     }
    //     std::string src_str(ip_str);
    //     if (!inet_ntop(AF_INET, &event->connection.daddr, ip_str, sizeof(ip_str))) {
    //         perror("inet_ntop");
    //         return -1;
    //     }
    //     std::string dst_str(ip_str);
    //     if(self->source_to_conn_.find(std::make_pair(self->ipToUint32(dst_str), dport)) == self->source_to_conn_.end()){
    //       //print dst_str and event->connection.dport
    //       printf("Adding connection to the connection map during accept update: %s:%u\n", dst_str.c_str(), event->connection.dport);
    //       return 0;
    //       auto new_connection =
    //         new ConnectionInstance(0,                    // socket fd
    //                               0,                      // event flags
    //                               self->base_,           // event base
    //                               ConnectionInstance::ClientHandler,
    //                               nullptr,               // argument for handler
    //                               self->lite_core_,      // lite core instance
    //                               false,                 // is_server
    //                               self);                 // worker instance
    //     self->source_to_conn_[std::make_pair(self->ipToUint32(dst_str), dport)] = new_connection;
    //     self->lite_core_.live_connections_.insert(new_connection);
    //     self->conns_.insert(new_connection);
    //     }
    // } else{
    //     if (!inet_ntop(AF_INET, &event->connection.saddr, ip_str, sizeof(ip_str))) {
    //         perror("inet_ntop");
    //         return -1;
    //     }
    //     std::string src_str(ip_str);
    //     if (!inet_ntop(AF_INET, &event->connection.daddr, ip_str, sizeof(ip_str))) {
    //         perror("inet_ntop");
    //         return -1;
    //     }
    //     std::string dest_str(ip_str);
    //     if(self->source_to_conn_.find(std::make_pair(self->ipToUint32(dest_str), dport)) != self->source_to_conn_.end()){
    //       //print dest_str and event->connection.dport
    //       printf("Removing connection from the connection map: %s:%u\n", dest_str.c_str(), dport);
    //       return 0;
    //       auto connection = self->source_to_conn_[std::make_pair(self->ipToUint32(dest_str), dport)];
    //       self->source_to_conn_[std::make_pair(self->ipToUint32(dest_str), dport)] = nullptr;
    //       // self->lite_core_.live_connections_.erase(new_connection);
    //       // self->conns_.erase(new_connection);
    //       delete connection;
    //     }
    // }
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
int EbpfWorker<Application, Request, Response, ConnectionInfo, CacheKey,
            CacheEntry>::SetSocketInfo(uint64_t socket_fd, uint64_t pid) {
  uint32_t key=0;
  uint64_t value = socket_fd;
  if (bpf_map_update_elem(bpf_map__fd(skel->maps.socket_info), &key, &value, BPF_ANY) < 0) {
      std::cerr << "Failed to update mode value in map: " << strerror(errno) << std::endl;
      return -1;
  }
  key = 1;
  value = pid;
  if (bpf_map_update_elem(bpf_map__fd(skel->maps.socket_info), &key, &value, BPF_ANY) < 0) {
      std::cerr << "Failed to update mode value in map: " << strerror(errno) << std::endl;
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