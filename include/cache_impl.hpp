#pragma once

#include "cache.hpp"

namespace lite {

template <typename Application, typename Request, typename Response,
          typename ConnectionInfo, typename CacheKey, typename CacheEntry>
bool Cache<Application, Request, Response, ConnectionInfo, CacheKey,
           CacheEntry>::Add(const CacheKey &key, const CacheEntry &value,
                            bool in_transaction, bool log) {
  bip::offset_ptr<LogEntryInstance> dirty = nullptr;
  bip::offset_ptr<CacheStateInstance> state = nullptr;
  if (cache_inner_ptr_->emergency_mode_ptr_->load() && log) {
    dirty =
        cache_inner_ptr_->segment_mgr_->template construct<LogEntryInstance>(
            bip::anonymous_instance)(nullptr, ShmSharedPtr<Request>{},
                                     conn_head_->backend_conn_ptr);
  }

  if (!cache_inner_ptr_->Add(key, value, in_transaction, dirty, state)) {
    if (dirty) {
      cache_inner_ptr_->segment_mgr_->destroy_ptr(dirty.get());
    }
    return false;
  }

  if (dirty) {
    dirty->state = state.get();
    logger_inner_ptr_->Log(dirty.get(), conn_head_.get());
  }
  return true;
}

template <typename Application, typename Request, typename Response,
          typename ConnectionInfo, typename CacheKey, typename CacheEntry>
bool Cache<Application, Request, Response, ConnectionInfo, CacheKey,
           CacheEntry>::Get(const CacheKey &key, CacheEntry &value,
                            bool in_transaction) {
  return cache_inner_ptr_->Get(key, value, in_transaction);
}

template <typename Application, typename Request, typename Response,
          typename ConnectionInfo, typename CacheKey, typename CacheEntry>
bool Cache<Application, Request, Response, ConnectionInfo, CacheKey,
           CacheEntry>::Delete(const CacheKey &key, bool in_transaction) {
  bip::offset_ptr<LogEntryInstance> dirty = nullptr;

  if (!cache_inner_ptr_->Delete(key, in_transaction, dirty)) return false;

  if (dirty) {
    std::unique_lock<std::mutex> chr_lock(logger_inner_ptr_->chr_mutex_);
    dirty->Delink();
    chr_lock.unlock();
    cache_inner_ptr_->segment_mgr_->destroy_ptr(dirty);
  }
  return true;
}

template <typename Application, typename Request, typename Response,
          typename ConnectionInfo, typename CacheKey, typename CacheEntry>
bool Cache<Application, Request, Response, ConnectionInfo, CacheKey,
           CacheEntry>::Set(const CacheKey &key, const CacheEntry &value,
                            bool in_transaction, bool log) {
  if (Add(key, value, in_transaction, log)) return true;
  return Replace(key, value, in_transaction, log);
}

template <typename Application, typename Request, typename Response,
          typename ConnectionInfo, typename CacheKey, typename CacheEntry>
bool Cache<Application, Request, Response, ConnectionInfo, CacheKey,
           CacheEntry>::Replace(const CacheKey &key, const CacheEntry &value,
                                bool in_transaction, bool log) {
  bip::offset_ptr<LogEntryInstance> dirty = nullptr;
  bip::offset_ptr<CacheStateInstance> state = nullptr;
  if (cache_inner_ptr_->emergency_mode_ptr_->load() && log) {
    dirty =
        cache_inner_ptr_->segment_mgr_->template construct<LogEntryInstance>(
            bip::anonymous_instance)(nullptr, ShmSharedPtr<Request>{},
                                     conn_head_->backend_conn_ptr);
  }

  if (!cache_inner_ptr_->Replace(key, value, in_transaction, dirty,
                                 cache_inner_ptr_->emergency_mode_ptr_->load()
                                     ? &logger_inner_ptr_->chr_mutex_
                                     : nullptr,
                                 state)) {
    if (dirty) {
      cache_inner_ptr_->segment_mgr_->destroy_ptr(dirty.get());
    }
    return false;
  }

  if (dirty) {
    dirty->state = state.get();
    logger_inner_ptr_->Log(dirty.get(), conn_head_.get());
  }
  return true;
}

template <typename Application, typename Request, typename Response,
          typename ConnectionInfo, typename CacheKey, typename CacheEntry>
void Cache<Application, Request, Response, ConnectionInfo, CacheKey,
           CacheEntry>::ConstVisitAll(std::function<void(const CacheKey &,
                                                         const CacheEntry &)>
                                          visitor,
                                      bool in_transaction) {
  cache_inner_ptr_->ConstVisitAll(visitor, in_transaction);
}

}  // namespace lite