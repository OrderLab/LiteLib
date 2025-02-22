#pragma once

#include <boost/interprocess/managed_shared_memory.hpp>

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
  explicit Cache(bip::offset_ptr<CacheInnerInstance> cache_inner_ptr,
                 bip::offset_ptr<LoggerInnerInstance> logger_inner_ptr,
                 bip::offset_ptr<LogEntryInstance> conn_head)
      : cache_inner_ptr_(cache_inner_ptr),
        logger_inner_ptr_(logger_inner_ptr),
        conn_head_(conn_head) {}

  ~Cache() {}

  bool Add(const CacheKey &key, const CacheEntry &value,
           bool in_transaction = false, bool log = true);

  bool Get(const CacheKey &key, CacheEntry &value, bool in_transaction = false);

  // TODO: how to force the application to log it?
  bool Delete(const CacheKey &key, bool in_transaction = false);

  bool Replace(const CacheKey &key, const CacheEntry &value,
               bool in_transaction = false, bool log = true);

  bool Set(const CacheKey &key, const CacheEntry &value,
           bool in_transaction = false, bool log = true);

  void ConstVisitAll(
      std::function<void(const CacheKey &, const CacheEntry &)> visitor,
      bool in_transaction = false);

  bip::scoped_lock<bip::interprocess_sharable_mutex> TransactionLock() {
    return cache_inner_ptr_->TransactionLock();
  }

  bip::offset_ptr<SegmentManager> GetSegmentManager() {
    return cache_inner_ptr_->GetSegmentManager();
  }

 private:
  bip::offset_ptr<CacheInnerInstance> cache_inner_ptr_;
  bip::offset_ptr<LoggerInnerInstance> logger_inner_ptr_;

  bip::offset_ptr<LogEntryInstance> conn_head_;
};

}  // namespace lite