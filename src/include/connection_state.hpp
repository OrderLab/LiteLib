#pragma once

#include "logger_inner.hpp"
#include "network_utils.hpp"

namespace lite {

template <typename Application, typename Request, typename Response,
          typename ConnectionInfo, typename CacheKey, typename CacheEntry>
class ConnectionState {
  using LogEntryInstance = LogEntry<Application, Request, Response,
                                    ConnectionInfo, CacheKey, CacheEntry>;
  using LoggerInstance = Logger<Application, Request, Response, ConnectionInfo,
                                CacheKey, CacheEntry>;
  using CacheInstance = Cache<Application, Request, Response, ConnectionInfo,
                              CacheKey, CacheEntry>;
  using CacheInnerInstance = CacheInner<Application, Request, Response,
                                        ConnectionInfo, CacheKey, CacheEntry>;
  using LoggerInnerInstance = LoggerInner<Application, Request, Response,
                                          ConnectionInfo, CacheKey, CacheEntry>;

 private:
  bip::offset_ptr<SegmentManager> segment_mgr_;

 public:
  ConnectionState(bip::offset_ptr<CacheInnerInstance> cache_inner_ptr,
                  bip::offset_ptr<LoggerInnerInstance> logger_inner_ptr,
                  ShmVoidAllocator allocator);

  ~ConnectionState();

  bip::offset_ptr<LogEntryInstance>
      log_head_;  // TODO: don't put it in shared memory

  CacheInstance cache_;

  LoggerInstance logger_;

  /// The pending requests
  ShmThreadSafeQueue<bip::pair<ShmSharedPtr<Request>, bool>> pending_requests_;

  ConnectionInfo extra_app_info_;
};

template <typename Application, typename Request, typename Response,
          typename ConnectionInfo, typename CacheKey, typename CacheEntry>
class ConnectionStateStorage {
  using ConnectionStateInstance =
      ConnectionState<Application, Request, Response, ConnectionInfo, CacheKey,
                      CacheEntry>;
  using CacheInnerInstance = CacheInner<Application, Request, Response,
                                        ConnectionInfo, CacheKey, CacheEntry>;
  using LoggerInnerInstance = LoggerInner<Application, Request, Response,
                                          ConnectionInfo, CacheKey, CacheEntry>;

 public:
  ConnectionStateStorage(bip::offset_ptr<CacheInnerInstance> cache_inner_ptr,
                         bip::offset_ptr<LoggerInnerInstance> logger_inner_ptr,
                         ShmVoidAllocator allocator)
      : segment_mgr_(allocator.get_segment_manager()),
        cache_inner_ptr_(cache_inner_ptr),
        logger_inner_ptr_(logger_inner_ptr),
        state_map_(allocator) {}

  ConnectionStateInstance* Get(const network::TCPID& tcp_id);

  ConnectionStateInstance* Add(const network::TCPID& tcp_id);

  // Pointers get from Get() will be invalidated after Delete()
  bool Delete(const network::TCPID& tcp_id);

 private:
  struct ConnectionStateEntry {
    bip::offset_ptr<SegmentManager> segment_mgr_;
    bip::offset_ptr<ConnectionStateInstance> state_ptr_;

    ConnectionStateEntry(bip::offset_ptr<ConnectionStateInstance> state_ptr,
                         bip::offset_ptr<SegmentManager> segment_mgr)
        : segment_mgr_(segment_mgr), state_ptr_(state_ptr) {}

    ConnectionStateEntry(ConnectionStateEntry&& other) noexcept
        : segment_mgr_(other.segment_mgr_), state_ptr_(other.state_ptr_) {
      other.state_ptr_ = nullptr;
      other.segment_mgr_ = nullptr;
    }

    ~ConnectionStateEntry() {
      if (state_ptr_) {
        segment_mgr_->destroy_ptr(state_ptr_.get());
      }
    }
  };

  bip::offset_ptr<SegmentManager> segment_mgr_;
  bip::offset_ptr<CacheInnerInstance> cache_inner_ptr_;
  bip::offset_ptr<LoggerInnerInstance> logger_inner_ptr_;

  using MapAllocator =
      ShmAllocator<bip::pair<const network::TCPID, ConnectionStateEntry>>;
  boost::concurrent_flat_map<network::TCPID, ConnectionStateEntry,
                             boost::hash<network::TCPID>,
                             std::equal_to<network::TCPID>, MapAllocator>
      state_map_;
};

}  // namespace lite
