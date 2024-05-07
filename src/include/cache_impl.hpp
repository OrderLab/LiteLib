#pragma once

#include "cache.hpp"

namespace lite {

template <typename Application, typename Request, typename Response,
          typename ConnectionInfo, typename CacheKey, typename CacheEntry>
bool Cache<Application, Request, Response, ConnectionInfo, CacheKey,
           CacheEntry>::Add(const CacheKey &key, const CacheEntry &value,
                            bool in_transaction) {
  LogEntryInstance *dirty = nullptr;
  CacheStateInstance *state = nullptr;
  if (cache_inner_.emergency_mode_) {
    dirty =
        new LogEntryInstance(nullptr, nullptr, conn_head_->backend_conn_ptr);
  }

  if (!cache_inner_.Add(key, value, in_transaction, dirty, state)) {
    delete dirty;
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
    delete dirty;
  }
  return true;
}

template <typename Application, typename Request, typename Response,
          typename ConnectionInfo, typename CacheKey, typename CacheEntry>
bool Cache<Application, Request, Response, ConnectionInfo, CacheKey,
           CacheEntry>::Set(const CacheKey &key, const CacheEntry &value,
                            bool in_transaction) {
  if (Add(key, value, in_transaction)) return true;
  return Replace(key, value, in_transaction);
}

template <typename Application, typename Request, typename Response,
          typename ConnectionInfo, typename CacheKey, typename CacheEntry>
bool Cache<Application, Request, Response, ConnectionInfo, CacheKey,
           CacheEntry>::Replace(const CacheKey &key, const CacheEntry &value,
                                bool in_transaction) {
  LogEntryInstance *old_dirty = nullptr;
  LogEntryInstance *dirty = nullptr;
  CacheStateInstance *state = nullptr;
  if (cache_inner_.emergency_mode_) {
    dirty =
        new LogEntryInstance(nullptr, nullptr, conn_head_->backend_conn_ptr);
  }

  if (!cache_inner_.Replace(key, value, in_transaction, dirty, old_dirty,
                            state)) {
    delete dirty;
    return false;
  }

  if (dirty) {
    dirty->state = state;
    logger_inner_.Log(dirty, conn_head_);
  }
  if (old_dirty) {
    std::unique_lock<std::mutex> chr_lock(logger_inner_.chr_mutex_);
    old_dirty->Delink();
    chr_lock.unlock();
    delete old_dirty;
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