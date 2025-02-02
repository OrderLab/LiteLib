#pragma once

#include "cache.hpp"

namespace lite {

template <typename Application, typename Request, typename Response,
          typename ConnectionInfo, typename CacheKey, typename CacheEntry>
bool Cache<Application, Request, Response, ConnectionInfo, CacheKey,
           CacheEntry>::Add(const CacheKey &key, const CacheEntry &value,
                            bool in_transaction, bool log) {
  LogEntryInstance *dirty = nullptr;
  CacheStateInstance *state = nullptr;
  if (cache_inner_.emergency_mode_ && log) {
    void *ptr = cache_inner_.segment_mgr_->allocate(sizeof(LogEntryInstance));
    dirty = new (ptr)
        LogEntryInstance(nullptr, nullptr, conn_head_->backend_conn_ptr);
  }

  if (!cache_inner_.Add(key, value, in_transaction, dirty, state)) {
    if (dirty) {
      dirty->~LogEntryInstance();
      cache_inner_.segment_mgr_->deallocate(dirty);
    }
    return false;
  }

  if (dirty) {
    dirty->state = state;
    logger_inner_.Log(dirty, conn_head_);
  }
  return true;
}

template <typename Application, typename Request, typename Response,
          typename ConnectionInfo, typename CacheKey, typename CacheEntry>
bool Cache<Application, Request, Response, ConnectionInfo, CacheKey,
           CacheEntry>::Get(const CacheKey &key, CacheEntry &value,
                            bool in_transaction) {
  return cache_inner_.Get(key, value, in_transaction);
}

template <typename Application, typename Request, typename Response,
          typename ConnectionInfo, typename CacheKey, typename CacheEntry>
bool Cache<Application, Request, Response, ConnectionInfo, CacheKey,
           CacheEntry>::Delete(const CacheKey &key, bool in_transaction) {
  LogEntryInstance *dirty = nullptr;

  if (!cache_inner_.Delete(key, in_transaction, dirty)) return false;

  if (dirty) {
    std::unique_lock<std::mutex> chr_lock(logger_inner_.chr_mutex_);
    dirty->Delink();
    chr_lock.unlock();
    dirty->~LogEntryInstance();
    cache_inner_.segment_mgr_->deallocate(dirty);
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
  LogEntryInstance *dirty = nullptr;
  CacheStateInstance *state = nullptr;
  if (cache_inner_.emergency_mode_ && log) {
    void *ptr = cache_inner_.segment_mgr_->allocate(sizeof(LogEntryInstance));
    dirty = new (ptr)
        LogEntryInstance(nullptr, nullptr, conn_head_->backend_conn_ptr);
  }

  if (!cache_inner_.Replace(
          key, value, in_transaction, dirty,
          cache_inner_.emergency_mode_ ? &logger_inner_.chr_mutex_ : nullptr,
          state)) {
    if (dirty) {
      dirty->~LogEntryInstance();
      cache_inner_.segment_mgr_->deallocate(dirty);
    }
    return false;
  }

  if (dirty) {
    dirty->state = state;
    logger_inner_.Log(dirty, conn_head_);
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
  cache_inner_.ConstVisitAll(visitor, in_transaction);
}

}  // namespace lite