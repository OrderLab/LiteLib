#pragma once

#include <sys/eventfd.h>

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

 public:
  // TODO: support called by multiple threads
  EmbeddedServer(int number_of_workers, size_t shared_memory_size,
                 size_t max_item_count,
                 const std::chrono::milliseconds &sliding_window_size)
      : number_of_workers_(number_of_workers) {
    for (int i = 0; i < number_of_workers; i++) {
      workers_.push_back(std::make_unique<EmbeddedWorkerInstance>(i));
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

 private:
  std::vector<std::unique_ptr<EmbeddedWorkerInstance>> workers_;
  typename decltype(workers_)::iterator current_worker_;
  int number_of_workers_;
};

}  // namespace lite