#pragma once

#include <event.h>
#include <fcntl.h>
#include <netinet/tcp.h>
#include <signal.h>
#include <sysexits.h>

#include "server.hpp"

namespace lite {

template <typename Application, typename Request, typename Response,
          typename ConnectionInfo, typename CacheKey, typename CacheEntry>
  requires IsProtocolMessage<Request> && IsProtocolMessage<Response>
LiteServer<Application, Request, Response, ConnectionInfo, CacheKey,
           CacheEntry>::LiteServer(const size_t& nthreads,
                                   const size_t& max_item_count,
                                   Application& app, std::string& backend_addr,
                                   std::string& backend_port,
                                   const std::chrono::milliseconds
                                       sliding_window_size,
                                   const size_t replay_expected_rps,
                                   const double flow_control_ratio,
                                   const size_t n_replay_threads,
                                   const char pipe_path[], bool crash_recover)
    : lite_core_(app, max_item_count, backend_addr, backend_port, pipe_path,
                 barrier_, workers_, sliding_window_size, replay_expected_rps,
                 flow_control_ratio, n_replay_threads, crash_recover),
      barrier_(nthreads + 1,
               []() { LOG(INFO) << "Barrier completed" << std::endl; }) {
  struct event_config* ev_config;
  ev_config = event_config_new();
  event_config_set_flag(ev_config, EVENT_BASE_FLAG_NOLOCK);
  main_base_ = event_base_new_with_config(ev_config);
  event_config_free(ev_config);

  for (int i = 0; i < nthreads; i++) {
    workers_.emplace_back(new WorkerInstance(lite_core_, barrier_));
    (**workers_.rbegin()).Run();
  }
  next_worker_ = workers_.begin();
}

template <typename Application, typename Request, typename Response,
          typename ConnectionInfo, typename CacheKey, typename CacheEntry>
  requires IsProtocolMessage<Request> && IsProtocolMessage<Response>
bool LiteServer<Application, Request, Response, ConnectionInfo, CacheKey,
                CacheEntry>::Run(const char* port) {
  signal(SIGPIPE, SIG_IGN);

  int sfd;
  struct linger ling = {0, 0};
  struct addrinfo* ai;
  struct addrinfo* next;
  struct addrinfo hints = {.ai_flags = AI_PASSIVE, .ai_family = AF_UNSPEC};
  char port_buf[NI_MAXSERV];
  int error;
  int success = 0;
  int flags = 1;

  hints.ai_socktype = SOCK_STREAM;

  char* interface = nullptr;
  error = getaddrinfo(interface, port, &hints, &ai);
  if (error != 0) {
    if (error != EAI_SYSTEM)
      LOG(ERROR) << "getaddrinfo(): " << gai_strerror(error) << '\n';
    else
      PLOG(ERROR) << "getaddrinfo()";
    return 0;
  }

  for (next = ai; next; next = next->ai_next) {
    if ((sfd = NewSocket(next)) == -1) {
      /* getaddrinfo can return "junk" addresses,
       * we make sure at least one works before erroring.
       */
      if (errno == EMFILE) {
        /* ...unless we're out of fds */
        PLOG(ERROR) << "server_socket";
        exit(EX_OSERR);
      }
      continue;
    }

    setsockopt(sfd, SOL_SOCKET, SO_REUSEADDR, (void*)&flags, sizeof(flags));
    PLOG_IF(ERROR, setsockopt(sfd, SOL_SOCKET, SO_KEEPALIVE, (void*)&flags,
                              sizeof(flags)))
        << "setsockopt";

    PLOG_IF(ERROR,
            setsockopt(sfd, SOL_SOCKET, SO_LINGER, (void*)&ling, sizeof(ling)))
        << "setsockopt";

    PLOG_IF(ERROR, setsockopt(sfd, IPPROTO_TCP, TCP_NODELAY, (void*)&flags,
                              sizeof(flags)))
        << "setsockopt";

    if (bind(sfd, next->ai_addr, next->ai_addrlen) == -1) {
      if (errno != EADDRINUSE) {
        PLOG(ERROR) << "bind()";
        close(sfd);
        freeaddrinfo(ai);
        return 0;
      }
      close(sfd);
      continue;
    } else {
      success++;
      if (listen(sfd, 1024) == -1) {
        PLOG(ERROR) << "listen()";
        close(sfd);
        freeaddrinfo(ai);
        return 0;
      }
    }

    std::unique_ptr<ConnectionInstance> new_connection;
    LOG_IF(FATAL, !(new_connection = std::make_unique<ConnectionInstance>(
                        sfd, EV_READ | EV_PERSIST, main_base_, EventHandler,
                        this, lite_core_, false, nullptr)))
        << "failed to create listening connection\n";
    conns_.push(std::move(new_connection));
  }

  freeaddrinfo(ai);

  /* Return zero iff we detected no errors in starting up connections */
  if (!success) return 0;

  event_base_loop(main_base_, 0);
  event_base_free(main_base_);
  return 1;
}

template <typename Application, typename Request, typename Response,
          typename ConnectionInfo, typename CacheKey, typename CacheEntry>
  requires IsProtocolMessage<Request> && IsProtocolMessage<Response>
void LiteServer<Application, Request, Response, ConnectionInfo, CacheKey,
                CacheEntry>::DispatchNewConnection(const evutil_socket_t sfd) {
  (**next_worker_)
      .notify_queue_.push_back(
          {.type = WorkerMessage::Type::kNewClientConnection, .fd = sfd});
  uint64_t buf = 1;
  PLOG_IF(ERROR, write((**next_worker_).notify_event_fd, &buf,
                       sizeof(uint64_t)) != sizeof(uint64_t))
      << "failed writing to worker eventfd";

  next_worker_++;
  if (next_worker_ == workers_.end()) next_worker_ = workers_.begin();
}

template <typename Application, typename Request, typename Response,
          typename ConnectionInfo, typename CacheKey, typename CacheEntry>
  requires IsProtocolMessage<Request> && IsProtocolMessage<Response>
typename LiteServer<Application, Request, Response, ConnectionInfo, CacheKey,
                    CacheEntry>::CacheInstance*
LiteServer<Application, Request, Response, ConnectionInfo, CacheKey,
           CacheEntry>::GetCacheDecoupledFromAnyConnection() {
  return new CacheInstance(lite_core_.cache_inner_, lite_core_.logger_inner_,
                           nullptr);
}

template <typename Application, typename Request, typename Response,
          typename ConnectionInfo, typename CacheKey, typename CacheEntry>
  requires IsProtocolMessage<Request> && IsProtocolMessage<Response>
int LiteServer<Application, Request, Response, ConnectionInfo, CacheKey,
               CacheEntry>::NewSocket(struct addrinfo* addr_info) {
  evutil_socket_t sfd;
  int flags;

  if ((sfd = socket(addr_info->ai_family, addr_info->ai_socktype,
                    addr_info->ai_protocol)) == -1) {
    return -1;
  }

  if ((flags = fcntl(sfd, F_GETFL, 0)) < 0 ||
      fcntl(sfd, F_SETFL, flags | O_NONBLOCK) < 0) {
    PLOG(ERROR) << "setting O_NONBLOCK";
    close(sfd);
    return -1;
  }
  return sfd;
}

template <typename Application, typename Request, typename Response,
          typename ConnectionInfo, typename CacheKey, typename CacheEntry>
  requires IsProtocolMessage<Request> && IsProtocolMessage<Response>
void LiteServer<Application, Request, Response, ConnectionInfo, CacheKey,
                CacheEntry>::EventHandler(const evutil_socket_t fd,
                                          const short which, void* arg_conn) {
  ConnectionInstance* c = static_cast<ConnectionInstance*>(arg_conn);
  const auto new_conn_fd = c->Accept();
  if (new_conn_fd == -1) {
    PLOG(ERROR) << "accept";
    return;
  }
  // LOG(INFO) << "Accepted new connection: " << new_conn_fd << std::endl;
  reinterpret_cast<LiteServer*>(c->lite_server_)
      ->DispatchNewConnection(new_conn_fd);
}

}  // namespace lite