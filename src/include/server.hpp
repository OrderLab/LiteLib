#pragma once

#include <event.h>
#include <fcntl.h>
#include <netinet/tcp.h>
#include <signal.h>
#include <sysexits.h>

#include <lite.hpp>
#include <memory>
#include <queue>
#include <string>

#include "connection.hpp"
#include "service.hpp"
#include "worker.hpp"

template <typename Packet, typename Service>
class LevelDBServer {
  using Connection = lite::Connection<Packet, Service>;
  using LiteServer = lite::LiteServer<Service, std::shared_ptr<Packet>,
                                      Connection, evutil_socket_t&>;

 public:
  LevelDBServer& operator=(const LevelDBServer&) = delete;

  /// Construct the server with the given thread pool size and maximum.
  explicit LevelDBServer(const size_t& nthreads, const size_t& max_item_count,
                         std::string& backend_addr, std::string& backend_port)
      : service_(max_item_count, backend_addr, backend_port),
        lite_server_(service_, backend_port, "/tmp/lite_LevelDB"),
        backend_addr_(backend_addr),
        backend_port_(backend_port) {
    struct event_config* ev_config;
    ev_config = event_config_new();
    event_config_set_flag(ev_config, EVENT_BASE_FLAG_NOLOCK);
    main_base_ = event_base_new_with_config(ev_config);
    event_config_free(ev_config);

    for (int i = 0; i < nthreads; i++) {
      workers_.emplace_back(new Worker<Packet, LevelDBService>(
          lite_server_, backend_addr_, backend_port_));
      (**workers_.rbegin()).Run();
    }
    next_worker_ = workers_.begin();
  }

  /// Listen on the specified TCP port.
  bool Run(const char* port) {
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
      error = setsockopt(sfd, SOL_SOCKET, SO_KEEPALIVE, (void*)&flags,
                         sizeof(flags));
      if (error != 0) perror("setsockopt");

      error =
          setsockopt(sfd, SOL_SOCKET, SO_LINGER, (void*)&ling, sizeof(ling));
      if (error != 0) perror("setsockopt");

      error = setsockopt(sfd, IPPROTO_TCP, TCP_NODELAY, (void*)&flags,
                         sizeof(flags));
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

      std::unique_ptr<Connection> new_connection;
      std::string place_holder;
      if (!(new_connection = std::make_unique<Connection>(
                sfd, EV_READ | EV_PERSIST, main_base_, EventHandler, this,
                lite_server_, false, place_holder, place_holder))) {
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

  /// Dispatch a new connection to the next thread in round-robin order.
  void DispatchNewConnection(const evutil_socket_t sfd) {
    (**next_worker_).notify_queue_.enqueue(sfd);
    uint64_t buf = 1;
    if (write((**next_worker_).notify_event_fd, &buf, sizeof(uint64_t)) !=
        sizeof(uint64_t)) {
      perror("failed writing to worker eventfd");
      /* TODO: This is a fatal problem. Can it ever happen temporarily? */
    }

    next_worker_++;
    if (next_worker_ == workers_.end()) next_worker_ = workers_.begin();
  }

 private:
  static int NewSocket(struct addrinfo* addr_info) {
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

  std::string &backend_addr_, &backend_port_;

  /// The internal service implementation.
  LevelDBService service_;

  /// The internal lite server
  LiteServer lite_server_;

  /// The worker threads.
  std::vector<std::unique_ptr<Worker<Packet, Service>>> workers_;

  /// The next thread to use for a new connection.
  decltype(workers_)::iterator next_worker_;

  /// The event base for the server thread.
  struct event_base* main_base_;

  /// The queue of listening sockets.
  std::queue<std::unique_ptr<Connection>> conns_;

  /// Handle a new connection.
  static void EventHandler(const evutil_socket_t fd, const short which,
                           void* arg_conn) {
    Connection* c = static_cast<Connection*>(arg_conn);
    const auto new_conn_fd = c->Accept();
    if (new_conn_fd == -1) {
      perror("accept");
      return;
    }
    reinterpret_cast<LevelDBServer*>(c->server_)
        ->DispatchNewConnection(new_conn_fd);
  }
};