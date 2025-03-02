#pragma once

#include <sys/epoll.h>
#include <sys/eventfd.h>

#include <thread>
#include <vector>

#include "cache_inner.hpp"
#include "connection_state.hpp"
#include "embedded_worker.hpp"
#include "logger_inner.hpp"

namespace lite {

template <typename Application, typename Request, typename Response,
          typename ConnectionInfo, typename CacheKey, typename CacheEntry>
class EmbeddedServer {
  using EmbeddedWorkerInstance =
      EmbeddedWorker<Application, Request, Response, ConnectionInfo, CacheKey,
                     CacheEntry>;
  using CacheInnerInstance = CacheInner<Application, Request, Response,
                                        ConnectionInfo, CacheKey, CacheEntry>;
  using LoggerInnerInstance = LoggerInner<Application, Request, Response,
                                          ConnectionInfo, CacheKey, CacheEntry>;
  using ConnectionStateStorageInstance =
      ConnectionStateStorage<Application, Request, Response, ConnectionInfo,
                             CacheKey, CacheEntry>;
  using NormalUpdateFn =
      std::function<int(void *request, ConnectionInfo &,
                        Cache<Application, Request, Response, ConnectionInfo,
                              CacheKey, CacheEntry> *,
                        RequestDestructorFn)>;

 public:
  // TODO: support called by multiple threads
  EmbeddedServer(int number_of_workers, size_t shared_memory_size,
                 size_t max_item_count,
                 const std::chrono::milliseconds &sliding_window_size,
                 const std::string socket_path,
                 RequestDestructorFn RequestDestructor,
                 FlushWriteBufferFn FlushWriteBuffer,
                 ReinstallClientEventHandlerFn ReinstallClientEventHandler,
                 ReinstallListenerEventHandlerFn ReinstallListenerEventHandler,
                 NormalUpdateFn NormalUpdate)
      : number_of_workers_(number_of_workers),
        RequestDestructor(RequestDestructor),
        FlushWriteBuffer(FlushWriteBuffer),
        ReinstallClientEventHandler(ReinstallClientEventHandler),
        ReinstallListenerEventHandler(ReinstallListenerEventHandler),
        NormalUpdate(NormalUpdate),
        socket_path_(socket_path) {
    for (int i = 0; i < number_of_workers; i++) {
      workers_.push_back(std::make_unique<EmbeddedWorkerInstance>(
          i, RequestDestructor, NormalUpdate, connection_state_storage_ptr_,
          notified_workers_count_));
      workers_[i]->Run();
    }
    current_worker_ = workers_.begin();

    // Initialize shared memory
    shared_memory_ = SharedMemory(bip::open_or_create, "lite_shared_memory",
                                  shared_memory_size);
    emergency_mode_ptr_ = shared_memory_.find_or_construct<ShmAtomic<bool>>(
        "emergency_mode")(false);
    cache_inner_ptr_ = shared_memory_.find_or_construct<CacheInnerInstance>(
        "cache_inner")(max_item_count, emergency_mode_ptr_,
                       shared_memory_.get_segment_manager());
    logger_inner_ptr_ =
        shared_memory_.find_or_construct<LoggerInnerInstance>("logger_inner")(
            sliding_window_size, shared_memory_.get_segment_manager());
    connection_state_storage_ptr_ =
        shared_memory_.find_or_construct<ConnectionStateStorageInstance>(
            "connection_state_storage")(cache_inner_ptr_, logger_inner_ptr_,
                                        shared_memory_.get_segment_manager());
  }

  int SendMessageToNextWorker(EmbeddedWorkerMessage msg) {
    (*current_worker_)->message_queue.push_back(msg);
    (*current_worker_)->Notify();
    current_worker_ = std::next(current_worker_);
    if (current_worker_ == workers_.end()) {
      current_worker_ = workers_.begin();
    }
    return 0;
  }

  SharedMemory shared_memory_;
  ShmAtomic<bool> *emergency_mode_ptr_;
  CacheInnerInstance *cache_inner_ptr_;
  LoggerInnerInstance *logger_inner_ptr_;
  ConnectionStateStorageInstance *connection_state_storage_ptr_;

  FlushWriteBufferFn FlushWriteBuffer;
  ReinstallClientEventHandlerFn ReinstallClientEventHandler;
  ReinstallListenerEventHandlerFn ReinstallListenerEventHandler;

  std::set<int> listener_fds_;
  std::map<int, std::pair<network::TCPID, void *>> fd_to_tcp_id_and_arg_;

  int number_of_workers_;
  std::atomic<int> notified_workers_count_;  // used to ack workers to switch to
                                             // emergency mode

  std::string socket_path_;

  std::queue<std::pair<int, void *>> replay_conns_;
  std::map<int, void *> fd_to_listener_;

 private:
  std::vector<std::unique_ptr<EmbeddedWorkerInstance>> workers_;
  typename decltype(workers_)::iterator current_worker_;

  RequestDestructorFn RequestDestructor;
  NormalUpdateFn NormalUpdate;

 public:
  void TransitionToNormalMode(const int lite_fd) {
    std::thread transition_thread([this, lite_fd]() {
      int epoll_fd = epoll_create1(0);
      if (epoll_fd < 0) {
        LOG(ERROR) << "LiteSys: Failed to create epoll fd";
        close(lite_fd);
        return;
      }

      struct epoll_event ev;
      ev.events = EPOLLIN;
      ev.data.fd = lite_fd;
      if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, lite_fd, &ev) < 0) {
        LOG(ERROR) << "LiteSys: Failed to add lite_fd to epoll";
        close(epoll_fd);
        close(lite_fd);
        return;
      }

      auto [received_fds, lens] = network::ReceiveSockets(lite_fd);
      if (received_fds.empty()) {
        LOG(ERROR) << "LiteSys: Error receiving socket message";
        close(epoll_fd);
        close(lite_fd);
        return;
      }
      close(lite_fd);
      LOG(WARNING) << "LiteSys: Received " << lens[0] << " client FDs and "
                   << (lens[1] - lens[0]) << " listener FDs";

      for (int i = 0; i < lens[0]; i++) {
        auto [replay_fd, client] = replay_conns_.front();
        replay_conns_.pop();
        ReinstallClientEventHandler(client, 0);
        auto original_fd =
            network::CopyAndReplaceSocket(replay_fd, received_fds[i]);
        if (original_fd < 0) {
          LOG(ERROR) << "Failed to hijack client socket: replay_fd="
                     << replay_fd << ", new_fd=" << received_fds[i];
          continue;
        }
        close(original_fd);
        ReinstallClientEventHandler(client, 1);
        close(received_fds[i]);
      }

      if (fd_to_listener_.size() != lens[1] - lens[0]) {
        LOG(ERROR) << "LiteSys: Number of listener FDs mismatch";
        close(epoll_fd);
        return;
      }
      int id = lens[0];
      for (auto &[fd, listener] : fd_to_listener_) {
        ReinstallListenerEventHandler(listener, 0);
        auto original_fd =
            network::CopyAndReplaceSocket(fd, received_fds[id++]);
        if (original_fd < 0) {
          LOG(ERROR) << "Failed to hijack listener socket: fd=" << fd
                     << ", new_fd=" << received_fds[id - 1];
          continue;
        }
        close(original_fd);
        ReinstallListenerEventHandler(listener, 1);
        close(received_fds[id - 1]);
      }

      emergency_mode_ptr_->store(false);

      close(epoll_fd);

      // clear state replay connections
      while (!replay_conns_.empty()) {
        replay_conns_.pop();
      }
    });

    transition_thread.detach();
  }
};

}  // namespace lite