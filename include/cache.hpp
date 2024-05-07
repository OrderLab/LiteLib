#pragma once

#include "cache_inner.hpp"
#include "logger_inner.hpp"

namespace lite {

template <typename Application, typename Request, typename Response,
          typename ConnectionInfo, typename CacheKey, typename CacheEntry>
class Cache {  // A wrapper for CacheInner
  using LoggerInnerInstance = LoggerInner<Application, Request, Response,
                                          ConnectionInfo, CacheKey, CacheEntry>;
  using LogEntryInstance = LogEntry<Application, Request, Response,
                                    ConnectionInfo, CacheKey, CacheEntry>;
  using CacheInnerInstance = CacheInner<Application, Request, Response,
                                        ConnectionInfo, CacheKey, CacheEntry>;
  using CacheStateInstance = CacheState<Application, Request, Response,
                                        ConnectionInfo, CacheKey, CacheEntry>;

 public:
  explicit Cache(CacheInnerInstance &cache_inner,
                 LoggerInnerInstance &logger_inner,
                 LogEntryInstance *const conn_head)
      : cache_inner_(cache_inner),
        logger_inner_(logger_inner),
        conn_head_(conn_head) {}

  ~Cache() {}

  bool Add(const CacheKey &key, const CacheEntry &value,
           bool in_transaction = false);

  bool Get(const CacheKey &key, CacheEntry &value, bool in_transaction = false);

  // TODO: how to force the application to log it?
  bool Delete(const CacheKey &key, bool in_transaction = false);

  bool Replace(const CacheKey &key, const CacheEntry &value,
               bool in_transaction = false);

  bool Set(const CacheKey &key, const CacheEntry &value,
           bool in_transaction = false);

  void ConstVisitAll(
      std::function<void(const CacheKey &, const CacheEntry &)> visitor,
      bool in_transaction = false);

  std::unique_lock<std::shared_mutex> TransactionLock() {
    return cache_inner_.TransactionLock();
  }

 private:
  CacheInnerInstance &cache_inner_;
  LoggerInnerInstance &logger_inner_;

  LogEntryInstance *const conn_head_;
};

}  // namespace lite