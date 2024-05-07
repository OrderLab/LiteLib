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
           bool in_transaction = false);

  bool Get(const Key &key, CacheEntry &value, bool in_transaction = false);

  // TODO: how to force the application to log it?
  bool Delete(const Key &key, bool in_transaction = false);

  bool Replace(const Key &key, const CacheEntry &value,
               bool in_transaction = false);

  bool Set(const Key &key, const CacheEntry &value,
           bool in_transaction = false);

  void ConstVisitAll(
      std::function<void(const Key &, const CacheEntry &)> visitor,
      bool in_transaction = false);

  std::unique_lock<std::shared_mutex> TransactionLock() {
    return cache_inner_.TransactionLock();
  }

 private:
  CacheInnerInstance &cache_inner_;
  LoggerInnerInstance &logger_inner_;

  LoggerInnerInstance::LogEntry *const conn_head_;
};

}  // namespace lite