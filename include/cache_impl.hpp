#pragma once

#include "cache.hpp"

namespace lite {

template <typename Key, typename CacheEntry, typename Request>
bool Cache<Key, CacheEntry, Request>::Add(const Key &key,
                                          const CacheEntry &value,
                                          bool in_transaction) {
  typename LoggerInnerInstance::LogEntry *dirty = nullptr;
  typename CacheInnerInstance::State *state = nullptr;
  if (cache_inner_.emergency_mode_) {
    dirty = new LoggerInnerInstance::LogEntry(nullptr, nullptr,
                                              conn_head_->backend_conn_ptr);
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

template <typename Key, typename CacheEntry, typename Request>
bool Cache<Key, CacheEntry, Request>::Get(const Key &key, CacheEntry &value,
                                          bool in_transaction) {
  return cache_inner_.Get(key, value, in_transaction);
}

template <typename Key, typename CacheEntry, typename Request>
bool Cache<Key, CacheEntry, Request>::Delete(const Key &key,
                                             bool in_transaction) {
  typename LoggerInnerInstance::LogEntry *dirty = nullptr;

  if (!cache_inner_.Delete(key, in_transaction, dirty)) return false;

  if (dirty) {
    std::unique_lock<std::mutex> chr_lock(logger_inner_.chr_mutex_);
    dirty->Delink();
    chr_lock.unlock();
    delete dirty;
  }
  return true;
}

template <typename Key, typename CacheEntry, typename Request>
bool Cache<Key, CacheEntry, Request>::Set(const Key &key,
                                          const CacheEntry &value,
                                          bool in_transaction) {
  if (Add(key, value, in_transaction)) return true;
  return Replace(key, value, in_transaction);
}

template <typename Key, typename CacheEntry, typename Request>
bool Cache<Key, CacheEntry, Request>::Replace(const Key &key,
                                              const CacheEntry &value,
                                              bool in_transaction) {
  void *old_dirty_void = nullptr;
  typename LoggerInnerInstance::LogEntry *dirty = nullptr;
  typename CacheInnerInstance::State *state = nullptr;
  if (cache_inner_.emergency_mode_) {
    dirty = new LoggerInnerInstance::LogEntry(nullptr, nullptr,
                                              conn_head_->backend_conn_ptr);
  }

  if (!cache_inner_.Replace(key, value, in_transaction, dirty, old_dirty_void,
                            state)) {
    delete dirty;
    return false;
  }

  if (dirty) {
    dirty->state = state;
    logger_inner_.Log(dirty, conn_head_);
  }
  if (old_dirty_void) {
    typename LoggerInnerInstance::LogEntry *old_dirty =
        static_cast<LoggerInnerInstance::LogEntry *>(old_dirty_void);
    std::unique_lock<std::mutex> chr_lock(logger_inner_.chr_mutex_);
    old_dirty->Delink();
    chr_lock.unlock();
    delete old_dirty;
  }
  return true;
}

template <typename Key, typename CacheEntry, typename Request>
void Cache<Key, CacheEntry, Request>::ConstVisitAll(
    std::function<void(const Key &, const CacheEntry &)> visitor,
    bool in_transaction) {
  cache_inner_.ConstVisitAll(visitor, in_transaction);
}

}  // namespace lite