#pragma once

#include "cache_inner.hpp"
#include "logger_inner.hpp"

namespace lite {

template <typename Key, typename CacheEntry, typename Request>
class Cache {  // A wrapper for CacheInner
  using LoggerInnerInstance = LoggerInner<Request>;
  using CacheInnerInstance = CacheInner<Key, CacheEntry>;

 public:
  explicit Cache(CacheInnerInstance &cache_inner,
                 LoggerInnerInstance &logger_inner,
                 LoggerInnerInstance::LogEntry *const conn_head)
      : cache_inner_(cache_inner),
        logger_inner_(logger_inner),
        conn_head_(conn_head) {}

  ~Cache() {}

  bool Add(const Key &key, const CacheEntry &value,
           bool in_transaction = false) {
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

  bool Get(const Key &key, CacheEntry &value, bool in_transaction = false) {
    return cache_inner_.Get(key, value, in_transaction);
  }

  // TODO: how to force the application to log it?
  bool Delete(const Key &key, bool in_transaction = false) {
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

  bool Replace(const Key &key, const CacheEntry &value,
               bool in_transaction = false) {
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

  bool Set(const Key &key, const CacheEntry &value,
           bool in_transaction = false) {
    if (Add(key, value, in_transaction)) return true;
    return Replace(key, value, in_transaction);
  }

  void ConstVisitAll(
      std::function<void(const Key &, const CacheEntry &)> visitor,
      bool in_transaction = false) {
    cache_inner_.ConstVisitAll(visitor, in_transaction);
  }

  std::unique_lock<std::shared_mutex> TransactionLock() {
    return cache_inner_.TransactionLock();
  }

 private:
  CacheInnerInstance &cache_inner_;
  LoggerInnerInstance &logger_inner_;

  LoggerInnerInstance::LogEntry *const conn_head_;
};

}  // namespace lite