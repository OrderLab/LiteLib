#pragma once

#include "connection_state.hpp"

namespace lite {

template <typename Application, typename Request, typename Response,
          typename ConnectionInfo, typename CacheKey, typename CacheEntry>
ConnectionState<Application, Request, Response, ConnectionInfo, CacheKey,
                CacheEntry>::
    ConnectionState(bip::offset_ptr<CacheInnerInstance> cache_inner_ptr,
                    bip::offset_ptr<LoggerInnerInstance> logger_inner_ptr,
                    ShmVoidAllocator allocator)
    : log_entry_allocator_(allocator),
      log_head_(log_entry_allocator_.allocate_one()),
      cache_(cache_inner_ptr, logger_inner_ptr, log_head_),
      logger_(logger_inner_ptr, log_head_),
      extra_app_info_(allocator),
      pending_requests_(allocator) {
  new (log_head_.get())
      LogEntryInstance(nullptr, ShmSharedPtr<Request>{},
                       nullptr);  // TODO: use true backend_conn here
}

template <typename Application, typename Request, typename Response,
          typename ConnectionInfo, typename CacheKey, typename CacheEntry>
ConnectionState<Application, Request, Response, ConnectionInfo, CacheKey,
                CacheEntry>::~ConnectionState() {
  log_head_->~LogEntryInstance();
  log_entry_allocator_.deallocate_one(log_head_);
}

template <typename Application, typename Request, typename Response,
          typename ConnectionInfo, typename CacheKey, typename CacheEntry>
typename ConnectionStateStorage<Application, Request, Response, ConnectionInfo,
                                CacheKey, CacheEntry>::ConnectionStateInstance*
ConnectionStateStorage<Application, Request, Response, ConnectionInfo, CacheKey,
                       CacheEntry>::Get(const network::TCPID& tcp_id) {
  ConnectionStateInstance* ret = nullptr;
  state_map_.visit(tcp_id,
                   [&](auto& value) { ret = value.second.state_ptr_.get(); });
  return ret;
}

template <typename Application, typename Request, typename Response,
          typename ConnectionInfo, typename CacheKey, typename CacheEntry>
typename ConnectionStateStorage<Application, Request, Response, ConnectionInfo,
                                CacheKey, CacheEntry>::ConnectionStateInstance*
ConnectionStateStorage<Application, Request, Response, ConnectionInfo, CacheKey,
                       CacheEntry>::Add(const network::TCPID& tcp_id) {
  auto state = connection_state_allocator_.allocate_one();
  new (state.get()) ConnectionStateInstance(cache_inner_ptr_, logger_inner_ptr_,
                                            connection_state_allocator_);
  if (!state_map_.emplace(
          std::piecewise_construct, std::forward_as_tuple(tcp_id),
          std::forward_as_tuple(state, connection_state_allocator_))) {
    state->~ConnectionStateInstance();
    connection_state_allocator_.deallocate_one(state);
    return nullptr;
  }
  return state.get();
}

template <typename Application, typename Request, typename Response,
          typename ConnectionInfo, typename CacheKey, typename CacheEntry>
typename ConnectionStateStorage<Application, Request, Response, ConnectionInfo,
                                CacheKey, CacheEntry>::ConnectionStateInstance*
ConnectionStateStorage<Application, Request, Response, ConnectionInfo, CacheKey,
                       CacheEntry>::GetOrAdd(const network::TCPID& tcp_id) {
  ConnectionStateInstance* state = Get(tcp_id);
  if (!state) {
    state = Add(tcp_id);
  }
  return state;
}

template <typename Application, typename Request, typename Response,
          typename ConnectionInfo, typename CacheKey, typename CacheEntry>
bool ConnectionStateStorage<Application, Request, Response, ConnectionInfo,
                            CacheKey, CacheEntry>::Delete(const network::TCPID&
                                                              tcp_id) {
  return state_map_.erase(tcp_id);
}

}  // namespace lite