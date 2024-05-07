#pragma once

#include "server.hpp"

namespace lite {

template <typename Request, typename Response, typename Application,
          typename CacheKey, typename CacheEntry, typename ConnectionInfo>
LiteServer<Request, Response, Application, CacheKey, CacheEntry,
           ConnectionInfo>::LiteServer(const size_t& nthreads,
                                       const size_t& max_item_count,
                                       Application& app,
                                       std::string& backend_addr,
                                       std::string& backend_port,
                                       const char pipe_path[])
    : lite_core_(
          app, max_item_count, backend_addr, backend_port, pipe_path,
          [](ThreadSafeSet<void*>& live_connections) {
            live_connections.visit_all([&](void* const& c) {
              static_cast<ConnectionInstance*>(c)->ConnectBackend();
              std::cerr << "Connect backend "
                        << static_cast<ConnectionInstance*>(c)->backend_fd_
                        << " to "
                        << static_cast<ConnectionInstance*>(c)->client_fd_
                        << std::endl;
            });
          },
          [](ThreadSafeSet<void*>& live_connections) {
            std::cerr << "Disconnect from backend" << std::endl;
            live_connections.visit_all([&](void* const& c) {
              close(static_cast<ConnectionInstance*>(c)->backend_fd_);
              static_cast<ConnectionInstance*>(c)->backend_fd_ = -1;
            });
          },
          [](void* conn) {
            return static_cast<ConnectionInstance*>(conn)->backend_fd_;
          },
          [](void* conn, std::shared_ptr<Request> req) {
            static_cast<ConnectionInstance*>(conn)->pending_requests_.push_back(
                std::make_pair(req, false));
          },
          [&]() {
            std::cerr << "Replay barrier initialized" << std::endl;
            for (auto& worker : workers_) {
              worker->notify_queue_.enqueue(-1);
              uint64_t buf = 1;
              if (write(worker->notify_event_fd, &buf, sizeof(uint64_t)) !=
                  sizeof(uint64_t)) {
                perror("failed writing to worker eventfd");
              }
            }
            barrier_.arrive_and_wait();
          },
          [&]() { barrier_.arrive_and_wait(); }),
      barrier_(nthreads + 1,
               []() { std::cerr << "Replay barrier completed" << std::endl; }) {
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

template <typename Request, typename Response, typename Application,
          typename CacheKey, typename CacheEntry, typename ConnectionInfo>
bool LiteServer<Request, Response, Application, CacheKey, CacheEntry,
                ConnectionInfo>::Run(const char* port) {
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
      fprintf(stderr, "getaddrinfo(): %s\n", gai_strerror(error));
    else
      perror("getaddrinfo()");
    return 0;
  }

  for (next = ai; next; next = next->ai_next) {
    if ((sfd = NewSocket(next)) == -1) {
      /* getaddrinfo can return "junk" addresses,
       * we make sure at least one works before erroring.
       */
      if (errno == EMFILE) {
        /* ...unless we're out of fds */
        perror("server_socket");
        exit(EX_OSERR);
      }
      continue;
    }

    setsockopt(sfd, SOL_SOCKET, SO_REUSEADDR, (void*)&flags, sizeof(flags));
    error =
        setsockopt(sfd, SOL_SOCKET, SO_KEEPALIVE, (void*)&flags, sizeof(flags));
    if (error != 0) perror("setsockopt");

    error = setsockopt(sfd, SOL_SOCKET, SO_LINGER, (void*)&ling, sizeof(ling));
    if (error != 0) perror("setsockopt");

    error =
        setsockopt(sfd, IPPROTO_TCP, TCP_NODELAY, (void*)&flags, sizeof(flags));
    if (error != 0) perror("setsockopt");

    if (bind(sfd, next->ai_addr, next->ai_addrlen) == -1) {
      if (errno != EADDRINUSE) {
        perror("bind()");
        close(sfd);
        freeaddrinfo(ai);
        return 0;
      }
      close(sfd);
      continue;
    } else {
      success++;
      if (listen(sfd, 1024) == -1) {
        perror("listen()");
        close(sfd);
        freeaddrinfo(ai);
        return 0;
      }
    }

    std::unique_ptr<ConnectionInstance> new_connection;
    if (!(new_connection = std::make_unique<ConnectionInstance>(
              sfd, EV_READ | EV_PERSIST, main_base_, EventHandler, this,
              lite_core_, false))) {
      fprintf(stderr, "failed to create listening connection\n");
      exit(EXIT_FAILURE);
    }
    conns_.push(std::move(new_connection));
  }

  freeaddrinfo(ai);

  /* Return zero iff we detected no errors in starting up connections */
  if (!success) return 0;

  event_base_loop(main_base_, 0);
  event_base_free(main_base_);
  return 1;
}

template <typename Request, typename Response, typename Application,
          typename CacheKey, typename CacheEntry, typename ConnectionInfo>
void LiteServer<Request, Response, Application, CacheKey, CacheEntry,
                ConnectionInfo>::DispatchNewConnection(const evutil_socket_t
                                                           sfd) {
  (**next_worker_).notify_queue_.enqueue(sfd);
  uint64_t buf = 1;
  if (write((**next_worker_).notify_event_fd, &buf, sizeof(uint64_t)) !=
      sizeof(uint64_t)) {
    perror("failed writing to worker eventfd");
  }

  next_worker_++;
  if (next_worker_ == workers_.end()) next_worker_ = workers_.begin();
}

template <typename Request, typename Response, typename Application,
          typename CacheKey, typename CacheEntry, typename ConnectionInfo>
int LiteServer<Request, Response, Application, CacheKey, CacheEntry,
               ConnectionInfo>::NewSocket(struct addrinfo* addr_info) {
  evutil_socket_t sfd;
  int flags;

  if ((sfd = socket(addr_info->ai_family, addr_info->ai_socktype,
                    addr_info->ai_protocol)) == -1) {
    return -1;
  }

  if ((flags = fcntl(sfd, F_GETFL, 0)) < 0 ||
      fcntl(sfd, F_SETFL, flags | O_NONBLOCK) < 0) {
    perror("setting O_NONBLOCK");
    close(sfd);
    return -1;
  }
  return sfd;
}

template <typename Request, typename Response, typename Application,
          typename CacheKey, typename CacheEntry, typename ConnectionInfo>
void LiteServer<Request, Response, Application, CacheKey, CacheEntry,
                ConnectionInfo>::EventHandler(const evutil_socket_t fd,
                                              const short which,
                                              void* arg_conn) {
  ConnectionInstance* c = static_cast<ConnectionInstance*>(arg_conn);
  const auto new_conn_fd = c->Accept();
  if (new_conn_fd == -1) {
    perror("accept");
    return;
  }
  // std::cerr << "Accepted new connection: " << new_conn_fd << std::endl;
  reinterpret_cast<LiteServer*>(c->lite_server_)
      ->DispatchNewConnection(new_conn_fd);
}

}  // namespace lite