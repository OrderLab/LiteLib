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
    : segment_mgr_(allocator.get_segment_manager()),
      log_head_(
          segment_mgr_->construct<LogEntryInstance>(bip::anonymous_instance)(
              nullptr, ShmSharedPtr<Request>{},
              nullptr)),  // TODO: use true backend_conn here
      cache_(cache_inner_ptr, logger_inner_ptr, log_head_),
      logger_(logger_inner_ptr, log_head_),
      extra_app_info_(allocator),
      pending_requests_(allocator) {}

template <typename Application, typename Request, typename Response,
          typename ConnectionInfo, typename CacheKey, typename CacheEntry>
ConnectionState<Application, Request, Response, ConnectionInfo, CacheKey,
                CacheEntry>::~ConnectionState() {
  segment_mgr_->destroy_ptr(log_head_.get());
}

template <typename Application, typename Request, typename Response,
          typename ConnectionInfo, typename CacheKey, typename CacheEntry>
uint64_t
ConnectionStateStorage<Application, Request, Response, ConnectionInfo, CacheKey,
                       CacheEntry>::GetHash(const network::TCPID& tcp_id) {
  return (uint64_t)tcp_id.src_ip << 32 | (uint64_t)tcp_id.src_port << 16 |
         (uint64_t)tcp_id.dst_port;
}

template <typename Application, typename Request, typename Response,
          typename ConnectionInfo, typename CacheKey, typename CacheEntry>
typename ConnectionStateStorage<Application, Request, Response, ConnectionInfo,
                                CacheKey, CacheEntry>::ConnectionStateInstance*
ConnectionStateStorage<Application, Request, Response, ConnectionInfo, CacheKey,
                       CacheEntry>::Get(const network::TCPID& tcp_id) {
  ConnectionStateInstance* ret = nullptr;
  state_map_.visit(GetHash(tcp_id), [&](ConnectionStateEntry& entry) {
    ret = entry.state_ptr_.get();
  });
  return ret;
}

template <typename Application, typename Request, typename Response,
          typename ConnectionInfo, typename CacheKey, typename CacheEntry>
typename ConnectionStateStorage<Application, Request, Response, ConnectionInfo,
                                CacheKey, CacheEntry>::ConnectionStateInstance*
ConnectionStateStorage<Application, Request, Response, ConnectionInfo, CacheKey,
                       CacheEntry>::Add(const network::TCPID& tcp_id) {
  // network::TCPID* shared_tcp_id =
  //     segment_mgr_->construct<network::TCPID>(bip::anonymous_instance)(tcp_id);
  ConnectionStateInstance* state =
      segment_mgr_->construct<ConnectionStateInstance>(bip::anonymous_instance)(
          cache_inner_ptr_, logger_inner_ptr_, segment_mgr_.get());
  ConnectionStateEntry* entry = segment_mgr_->construct<ConnectionStateEntry>(
      bip::anonymous_instance)(state, segment_mgr_.get());
  state_map_.insert(std::make_pair(GetHash(tcp_id), boost::move(*entry)));
  segment_mgr_->destroy_ptr(entry);
  return state;
}

template <typename Application, typename Request, typename Response,
          typename ConnectionInfo, typename CacheKey, typename CacheEntry>
bool ConnectionStateStorage<Application, Request, Response, ConnectionInfo,
                            CacheKey, CacheEntry>::Delete(const network::TCPID&
                                                              tcp_id) {
  return state_map_.erase(GetHash(tcp_id));
}

}  // namespace lite