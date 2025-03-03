#pragma once
#include <event2/event.h>
#include <sys/eventfd.h>

#include <memory>

#include "connection_state.hpp"
#include "embedded_lite.h"
#include "thread_safe_queue.hpp"

namespace lite {
struct EmbeddedWorkerMessage {
  enum class Type {
    kNormalUpdate,
    kConnectionDisconnect,
    kSwitchToEmergencyMode,
  };

  Type type;

  void* data;
};

template <typename Application, typename Request, typename Response,
          typename ConnectionInfo, typename CacheKey, typename CacheEntry>
struct EmbeddedNormalUpdateMessage {
  void* conn_info;
  void* request;
};

struct EmbeddedConnectionDisconnectMessage {
  network::TCPID tcp_id;
};

template <typename Application, typename Request, typename Response,
          typename ConnectionInfo, typename CacheKey, typename CacheEntry>
class EmbeddedWorker {
  using ConnectionStateStorageInstance =
      ConnectionStateStorage<Application, Request, Response, ConnectionInfo,
                             CacheKey, CacheEntry>;
  using NormalUpdateFn =
      std::function<int(void* request, ConnectionInfo&,
                        Cache<Application, Request, Response, ConnectionInfo,
                              CacheKey, CacheEntry>*,
                        RequestDestructorFn)>;

 public:
  EmbeddedWorker(int id, RequestDestructorFn RequestDestructor,
                 NormalUpdateFn NormalUpdate,
                 ConnectionStateStorageInstance*& connection_state_storage_ptr,
                 std::atomic<int>& notified_workers_count)
      : id_(id),
        RequestDestructor(RequestDestructor),
        NormalUpdate(NormalUpdate),
        notified_workers_count_(notified_workers_count),
        event_base_(nullptr),
        event_(nullptr),
        connection_state_storage_ptr_(connection_state_storage_ptr) {
    event_base_ = event_base_new();

    PCHECK(event_fd_ = eventfd(0, EFD_NONBLOCK))
        << "failed creating eventfd for worker thread";

    event_ = event_new(event_base_, event_fd_, EV_READ | EV_PERSIST,
                       &EmbeddedWorker::EventCallback, this);
    LOG_IF(FATAL, !event_ || event_add(event_, nullptr) < 0)
        << "failed to create/add event";
  }

  void Notify() {
    uint64_t value = 1;
    PCHECK(write(event_fd_, &value, sizeof(value)) == sizeof(value))
        << "failed to write to eventfd";
  }

  void Run() {
    pthread_attr_t attr;

    pthread_attr_init(&attr);

    PCHECK(!pthread_create(&thread_id_, &attr, ThreadBody, this))
        << "Can't create thread: lite-worker";

    pthread_setname_np(thread_id_, "lite-worker");
    pthread_attr_destroy(&attr);
  }

  ~EmbeddedWorker() {
    if (event_) {
      event_free(event_);
    }
    if (event_base_) {
      event_base_free(event_base_);
    }
    if (event_fd_ != -1) {
      close(event_fd_);
    }
  }

  ThreadSafeQueue<EmbeddedWorkerMessage> message_queue;

 private:
  static void* ThreadBody(void* arg) {
    EmbeddedWorker* worker = static_cast<EmbeddedWorker*>(arg);

    event_base_loop(worker->event_base_, 0);
    event_base_free(worker->event_base_);

    return nullptr;
  }

  static void EventCallback(evutil_socket_t fd, short events, void* arg) {
    EmbeddedWorker* worker = static_cast<EmbeddedWorker*>(arg);
    uint64_t value;
    if (read(fd, &value, sizeof(value)) != sizeof(value)) {
      throw std::runtime_error("Failed to read from eventfd");
    }

    // Process all pending messages
    while (value--) {
      worker->HandleEvent();
    }
  }

  void HandleEvent() {
    EmbeddedWorkerMessage msg = message_queue.pop_front();
    switch (msg.type) {
      case EmbeddedWorkerMessage::Type::kNormalUpdate: {
        auto job = static_cast<
            EmbeddedNormalUpdateMessage<Application, Request, Response,
                                        ConnectionInfo, CacheKey, CacheEntry>*>(
            msg.data);
        auto connection_state_ptr =
            static_cast<ConnectionState<Application, Request, Response,
                                        ConnectionInfo, CacheKey, CacheEntry>*>(
                job->conn_info);
        auto ret =
            NormalUpdate(job->request, connection_state_ptr->extra_app_info_,
                         &connection_state_ptr->cache_, RequestDestructor);
        delete job;
        break;
      }
      case EmbeddedWorkerMessage::Type::kConnectionDisconnect: {
        auto job = static_cast<EmbeddedConnectionDisconnectMessage*>(msg.data);
        connection_state_storage_ptr_->Delete(job->tcp_id);
        delete job;
        break;
      }
      case EmbeddedWorkerMessage::Type::kSwitchToEmergencyMode: {
        if (!message_queue.empty()) {
          LOG(WARNING) << "Embedded worker " << id_
                       << " has pending messages when switching to emergency "
                          "mode, which might due to a never-stop full "
                          "version's thread and may cause data loss";
        }
        LOG(INFO) << "Embedded worker " << id_ << " switches to emergency mode";
        notified_workers_count_++;
        event_base_loopexit(event_base_, nullptr);
        break;
      }
    }
  }

  int id_;
  pthread_t thread_id_;
  int event_fd_;
  struct event_base* event_base_;
  struct event* event_;

  std::atomic<int>& notified_workers_count_;

  RequestDestructorFn RequestDestructor;
  NormalUpdateFn NormalUpdate;

  ConnectionStateStorageInstance*& connection_state_storage_ptr_;
};

}  // namespace lite