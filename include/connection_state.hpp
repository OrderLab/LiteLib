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
  ShmAllocator<LogEntryInstance> log_entry_allocator_;

 public:
  ConnectionState(bip::offset_ptr<CacheInnerInstance> cache_inner_ptr,
                  bip::offset_ptr<LoggerInnerInstance> logger_inner_ptr,
                  ShmVoidAllocator allocator);

  ~ConnectionState();

  bip::offset_ptr<LogEntryInstance> log_head_;

  CacheInstance cache_;

  LoggerInstance logger_;

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
      : connection_state_allocator_(allocator),
        cache_inner_ptr_(cache_inner_ptr),
        logger_inner_ptr_(logger_inner_ptr),
        state_map_(allocator),
        replay_conns_(allocator) {}

  ConnectionStateInstance* Get(const network::TCPID& tcp_id);

  ConnectionStateInstance* Add(const network::TCPID& tcp_id);

  ConnectionStateInstance* GetOrAdd(const network::TCPID& tcp_id);

  // Pointers get from Get() will be invalidated after Delete()
  bool Delete(const network::TCPID& tcp_id);

  ShmThreadSafeQueue<network::TCPID> replay_conns_;

 private:
  struct ConnectionStateEntry {
    ShmAllocator<ConnectionStateInstance> connection_state_allocator_;
    bip::offset_ptr<ConnectionStateInstance> state_ptr_;

    ConnectionStateEntry(bip::offset_ptr<ConnectionStateInstance> state_ptr,
                         ShmAllocator<ConnectionStateInstance> allocator)
        : connection_state_allocator_(allocator), state_ptr_(state_ptr) {}

    ConnectionStateEntry(ConnectionStateEntry&& other) noexcept
        : connection_state_allocator_(other.connection_state_allocator_),
          state_ptr_(other.state_ptr_) {
      other.state_ptr_ = nullptr;
    }

    ~ConnectionStateEntry() {
      if (state_ptr_) {
        state_ptr_->~ConnectionStateInstance();
        connection_state_allocator_.deallocate_one(state_ptr_);
      }
    }
  };

  ShmAllocator<ConnectionStateInstance> connection_state_allocator_;

  bip::offset_ptr<CacheInnerInstance> cache_inner_ptr_;
  bip::offset_ptr<LoggerInnerInstance> logger_inner_ptr_;

  using MapAllocator =
      ShmAllocator<std::pair<const network::TCPID, ConnectionStateEntry>>;
  boost::concurrent_flat_map<network::TCPID, ConnectionStateEntry,
                             boost::hash<network::TCPID>,
                             std::equal_to<network::TCPID>, MapAllocator>
      state_map_;
};

}  // namespace lite
