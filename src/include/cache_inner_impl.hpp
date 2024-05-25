#pragma once

#include <iostream>

#include "cache_inner.hpp"

namespace lite {

template <typename Application, typename Request, typename Response,
          typename ConnectionInfo, typename CacheKey, typename CacheEntry>
CacheInner<Application, Request, Response, ConnectionInfo, CacheKey,
           CacheEntry>::CacheInner(const size_t &max_size,
                                   std::atomic<bool> &emergency_mode)
    : max_size_(max_size), size(0), emergency_mode_(emergency_mode) {
  lru_head_.pre_ = nullptr;
  lru_head_.nxt_ = &lru_tail_;
  lru_tail_.pre_ = &lru_head_;
}

template <typename Application, typename Request, typename Response,
          typename ConnectionInfo, typename CacheKey, typename CacheEntry>
CacheInner<Application, Request, Response, ConnectionInfo, CacheKey,
           CacheEntry>::~CacheInner() {
  ListNode *node = lru_head_.nxt_;
  ListNode *nxt;

  while (node != &lru_tail_) {
    nxt = node->nxt_;
    delete node;
    node = nxt;
  }
}

template <typename Application, typename Request, typename Response,
          typename ConnectionInfo, typename CacheKey, typename CacheEntry>
bool CacheInner<Application, Request, Response, ConnectionInfo, CacheKey,
                CacheEntry>::Add(const CacheKey &key, const CacheEntry &value,
                                 const bool in_transaction,
                                 LogEntryInstance *dirty_node,
                                 CacheStateInstance *&new_state) {
  std::shared_lock<std::shared_mutex> transaction_lock;
  if (!in_transaction) {
    transaction_lock = std::shared_lock<std::shared_mutex>{transaction_mutex_};
  }
  MapEntry entry = MapEntry(key, value, dirty_node,
                            HasGetSize<CacheEntry> ? value.GetSize() : 0);
  new_state = entry.state.get();
  ListNode *lru_node = entry.lru_node;
  if (!cache_.insert(std::make_pair(key, std::move(entry)))) {
    delete lru_node;
    return false;
  }

  std::unique_lock<std::mutex> lru_lock(lru_mutex_);
  lru_node->PushFront(lru_head_);
  if constexpr (HasGetSize<CacheEntry>) {
    size += lru_node->state_->size;
  } else {
    size++;
  }
  if (size > max_size_) Evict();
  lru_lock.unlock();

  return true;
}

template <typename Application, typename Request, typename Response,
          typename ConnectionInfo, typename CacheKey, typename CacheEntry>
bool CacheInner<Application, Request, Response, ConnectionInfo, CacheKey,
                CacheEntry>::Get(const CacheKey &key, CacheEntry &value,
                                 bool in_transaction) {
  std::shared_lock<std::shared_mutex> transaction_lock;
  if (!in_transaction) {
    transaction_lock = std::shared_lock<std::shared_mutex>{transaction_mutex_};
  }
  return cache_.cvisit(key, [this, &value](auto &element) {
    value = element.second.state->value;

    std::unique_lock<std::mutex> lru_lock(lru_mutex_, std::try_to_lock);
    if (lru_lock) {
      ListNode *lru_node = element.second.lru_node;
      // The list node may be out of the list if it is in the process of being
      // inserted or evicted. Doing this check allows us to lock the list for
      // shorter periods of time.
      if (lru_node->isInList()) {
        lru_node->Delink();
        lru_node->PushFront(lru_head_);
      }
      lru_lock.unlock();
    }
  });
}

template <typename Application, typename Request, typename Response,
          typename ConnectionInfo, typename CacheKey, typename CacheEntry>
bool CacheInner<Application, Request, Response, ConnectionInfo, CacheKey,
                CacheEntry>::Delete(const CacheKey &key, bool in_transaction,
                                    LogEntryInstance *&dirty_node) {
  std::shared_lock<std::shared_mutex> transaction_lock;
  if (!in_transaction) {
    transaction_lock = std::shared_lock<std::shared_mutex>{transaction_mutex_};
  }
  ListNode *lru_node = nullptr;
  cache_.cvisit(key, [&](auto &element) {
    lru_node = element.second.lru_node;
    dirty_node = element.second.dirty_node;
  });
  if (!lru_node || !cache_.erase(key)) return false;

  std::unique_lock<std::mutex> lru_lock(lru_mutex_);
  lru_node->Delink();
  if constexpr (HasGetSize<CacheEntry>) {
    size -= lru_node->state_->size;
  } else {
    size--;
  }
  lru_lock.unlock();
  delete lru_node;

  return true;
}

template <typename Application, typename Request, typename Response,
          typename ConnectionInfo, typename CacheKey, typename CacheEntry>
bool CacheInner<Application, Request, Response, ConnectionInfo, CacheKey,
                CacheEntry>::Replace(const CacheKey &key,
                                     const CacheEntry &value,
                                     bool in_transaction,
                                     LogEntryInstance *dirty_node,
                                     std::mutex *logger_chr_mutex,
                                     CacheStateInstance *&new_state) {
  std::shared_lock<std::shared_mutex> transaction_lock;
  if (!in_transaction) {
    transaction_lock = std::shared_lock<std::shared_mutex>{transaction_mutex_};
  }
  bool ret = false;
  cache_.visit(key, [&](auto &element) {
    element.second.state->value = value;

    std::unique_lock<std::mutex> chr_lock;
    if (logger_chr_mutex) {
      chr_lock = std::unique_lock<std::mutex>{*logger_chr_mutex};
    }
    auto old_dirty_node = element.second.state->dirty_node;
    element.second.state->dirty_node = dirty_node;
    if (old_dirty_node) {
      old_dirty_node->Delink();
    }
    if (logger_chr_mutex) {
      chr_lock.unlock();
    }
    if (old_dirty_node) {
      delete old_dirty_node;
    }

    size_t new_size;
    if constexpr (HasGetSize<CacheEntry>) {
      new_size = value.GetSize();
    }
    ret = true;

    std::unique_lock<std::mutex> lru_lock(lru_mutex_, std::try_to_lock);
    if (lru_lock) {
      ListNode *lru_node = element.second.lru_node;
      // The list node may be out of the list if it is in the process of being
      // inserted or evicted. Doing this check allows us to lock the list for
      // shorter periods of time.
      if (lru_node->isInList()) {
        lru_node->Delink();
        lru_node->PushFront(lru_head_);
        if constexpr (HasGetSize<CacheEntry>) {
          size += new_size - lru_node->size;
          lru_node->size = new_size;
        }
      }
      lru_lock.unlock();
    }

    new_state = element.second.state.get();
  });

  if (size > max_size_) {
    std::unique_lock<std::mutex> lru_lock(lru_mutex_, std::try_to_lock);
    if (lru_lock && size > max_size_) Evict();
  }

  return ret;
}

template <typename Application, typename Request, typename Response,
          typename ConnectionInfo, typename CacheKey, typename CacheEntry>
void CacheInner<Application, Request, Response, ConnectionInfo, CacheKey,
                CacheEntry>::
    ConstVisitAll(
        std::function<void(const CacheKey &, const CacheEntry &)> visitor,
        bool in_transaction) {
  std::shared_lock<std::shared_mutex> transaction_lock;
  if (!in_transaction) {
    transaction_lock = std::shared_lock<std::shared_mutex>{transaction_mutex_};
  }
  cache_.visit_all([&](auto &x) { visitor(x.first, x.second.value); });
}

template <typename Application, typename Request, typename Response,
          typename ConnectionInfo, typename CacheKey, typename CacheEntry>
void CacheInner<Application, Request, Response, ConnectionInfo, CacheKey,
                CacheEntry>::
    VisitAllState(
        std::function<void(CacheStateInstance *)> visitor,
        bool in_transaction) {
  std::shared_lock<std::shared_mutex> transaction_lock;
  if (!in_transaction) {
    transaction_lock = std::shared_lock<std::shared_mutex>{transaction_mutex_};
  }
  cache_.visit_all([&](auto &x) { visitor(x.second.state.get()); });
}

template <typename Application, typename Request, typename Response,
          typename ConnectionInfo, typename CacheKey, typename CacheEntry>
void CacheInner<Application, Request, Response, ConnectionInfo, CacheKey,
                CacheEntry>::Evict() {
  if (emergency_mode_) return;
  size_t threshold = 10;  // prevent blocking for too long
  // TODO: choose a better threshold
  while (size > max_size_ && threshold--) {
    ListNode *moribund = lru_tail_.pre_;
    if (moribund == &lru_head_) {
      // List is empty, can't evict
      return;
    }
    moribund->Delink();
    if constexpr (HasGetSize<CacheEntry>) {
      size -= moribund->size;
    } else {
      size--;
    }

    cache_.erase(moribund->state_->key);
    delete moribund;
  }
  // LOG(INFO) << "Evict done: " << size << std::endl;
}

}  // namespace lite