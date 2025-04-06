#pragma once
#include <atomic>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <thread>

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
        running_(true),
        connection_state_storage_ptr_(connection_state_storage_ptr) {}

  void Notify() { cv_.notify_one(); }

  void Run() {
    thread_ = std::thread(&EmbeddedWorker::ThreadBody, this);
    pthread_setname_np(thread_.native_handle(), "lite-worker");
  }

  ~EmbeddedWorker() {
    {
      std::lock_guard<std::mutex> lock(mutex_);
      running_ = false;
    }
    cv_.notify_one();
    if (thread_.joinable()) {
      thread_.join();
    }
  }

  ThreadSafeQueue<EmbeddedWorkerMessage> message_queue;

 private:
  static void* ThreadBody(void* arg) {
    EmbeddedWorker* worker = static_cast<EmbeddedWorker*>(arg);
    worker->ProcessMessages();
    return nullptr;
  }

  void ProcessMessages() {
    while (true) {
      {
        std::unique_lock<std::mutex> lock(mutex_);
        cv_.wait(lock,
                 [this]() { return !running_ || !message_queue.empty(); });
        if (!running_ && message_queue.empty()) {
          break;
        }
      }
      while (!message_queue.empty()) {
        HandleEvent();
      }
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
        if (!connection_state_storage_ptr_->Delete(job->tcp_id)) {
          LOG(WARNING) << "Connection not registered";
        }
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
        {
          std::lock_guard<std::mutex> lock(mutex_);
          running_ = false;
        }
        break;
      }
    }
  }

  int id_;
  std::thread thread_;

  std::mutex mutex_;
  std::condition_variable cv_;
  bool running_;

  std::atomic<int>& notified_workers_count_;

  RequestDestructorFn RequestDestructor;
  NormalUpdateFn NormalUpdate;

  ConnectionStateStorageInstance*& connection_state_storage_ptr_;
};

}  // namespace lite