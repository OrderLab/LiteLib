#pragma once

#include "cache.hpp"

namespace lite {

template <typename Application, typename Request, typename Response,
          typename ConnectionInfo, typename CacheKey, typename CacheEntry>
bip::scoped_lock<bip::interprocess_sharable_mutex>
Cache<Application, Request, Response, ConnectionInfo, CacheKey,
      CacheEntry>::LockEmergencyMutation(bool &in_transaction) {
  bip::scoped_lock<bip::interprocess_sharable_mutex> lock;
  if (!in_transaction && cache_inner_ptr_->emergency_mode_ptr_->load()) {
    lock = cache_inner_ptr_->TransactionLock();
    in_transaction = true;
  }
  return lock;
}

template <typename Application, typename Request, typename Response,
          typename ConnectionInfo, typename CacheKey, typename CacheEntry>
bool Cache<Application, Request, Response, ConnectionInfo, CacheKey,
           CacheEntry>::Add(const CacheKey &key, const CacheEntry &value,
                            bool in_transaction, bool log) {
  auto mutation_lock = LockEmergencyMutation(in_transaction);
  bip::offset_ptr<LogEntryInstance> dirty = nullptr;
  bip::offset_ptr<CacheStateInstance> state = nullptr;
  if (cache_inner_ptr_->emergency_mode_ptr_->load() && log) {
    dirty = cache_inner_ptr_->log_entry_allocator_.allocate_one();
    new (dirty.get()) LogEntryInstance(nullptr, ShmSharedPtr<Request>{},
                                       conn_head_->backend_conn_ptr);
  }

  if (!cache_inner_ptr_->Add(key, value, in_transaction, dirty, state)) {
    if (dirty) {
      dirty->~LogEntryInstance();
      cache_inner_ptr_->log_entry_allocator_.deallocate_one(dirty);
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
  bip::scoped_lock<bip::interprocess_sharable_mutex> mutation_lock;
  if (!in_transaction) {
    mutation_lock = cache_inner_ptr_->TransactionLock();
    in_transaction = true;
  }
  bip::offset_ptr<LogEntryInstance> dirty = nullptr;

  if (!cache_inner_ptr_->Delete(key, in_transaction, dirty)) return false;

  if (dirty) {
    bip::scoped_lock<bip::interprocess_mutex> chr_lock(
        logger_inner_ptr_->chr_mutex_);
    dirty->Delink();
    chr_lock.unlock();
    dirty->~LogEntryInstance();
    cache_inner_ptr_->log_entry_allocator_.deallocate_one(dirty);
  }
  return true;
}

template <typename Application, typename Request, typename Response,
          typename ConnectionInfo, typename CacheKey, typename CacheEntry>
bool Cache<Application, Request, Response, ConnectionInfo, CacheKey,
           CacheEntry>::Replace(const CacheKey &key, const CacheEntry &value,
                                bool in_transaction, bool log) {
  auto mutation_lock = LockEmergencyMutation(in_transaction);
  bip::offset_ptr<LogEntryInstance> dirty = nullptr;
  bip::offset_ptr<CacheStateInstance> state = nullptr;
  if (cache_inner_ptr_->emergency_mode_ptr_->load() && log) {
    dirty = cache_inner_ptr_->log_entry_allocator_.allocate_one();
    new (dirty.get()) LogEntryInstance(nullptr, ShmSharedPtr<Request>{},
                                       conn_head_->backend_conn_ptr);
  }

  if (!cache_inner_ptr_->Replace(key, value, in_transaction, dirty,
                                 cache_inner_ptr_->emergency_mode_ptr_->load()
                                     ? &logger_inner_ptr_->chr_mutex_
                                     : nullptr,
                                 state)) {
    if (dirty) {
      dirty->~LogEntryInstance();
      cache_inner_ptr_->log_entry_allocator_.deallocate_one(dirty);
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
           CacheEntry>::Set(const CacheKey &key, const CacheEntry &value,
                            bool in_transaction, bool log) {
  auto mutation_lock = LockEmergencyMutation(in_transaction);
  bip::offset_ptr<LogEntryInstance> dirty = nullptr;
  bip::offset_ptr<CacheStateInstance> state = nullptr;
  if (cache_inner_ptr_->emergency_mode_ptr_->load() && log) {
    dirty = cache_inner_ptr_->log_entry_allocator_.allocate_one();
    new (dirty.get()) LogEntryInstance(nullptr, ShmSharedPtr<Request>{},
                                       conn_head_->backend_conn_ptr);
  }

  if (!cache_inner_ptr_->Set(key, value, in_transaction, dirty,
                             cache_inner_ptr_->emergency_mode_ptr_->load()
                                 ? &logger_inner_ptr_->chr_mutex_
                                 : nullptr,
                             state)) {
    if (dirty) {
      dirty->~LogEntryInstance();
      cache_inner_ptr_->log_entry_allocator_.deallocate_one(dirty);
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