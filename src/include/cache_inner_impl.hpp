#pragma once

#include <iostream>

#include "cache_inner.hpp"

namespace lite {

template <typename Application, typename Request, typename Response,
          typename ConnectionInfo, typename CacheKey, typename CacheEntry>
CacheInner<Application, Request, Response, ConnectionInfo, CacheKey,
           CacheEntry>::CacheInner(const size_t &max_size,
                                   bip::offset_ptr<ShmAtomic<bool>>
                                       emergency_mode_ptr,
                                   bip::offset_ptr<SegmentManager> segment_mgr)
    : max_size_(max_size),
      size(0),
      emergency_mode_ptr_(emergency_mode_ptr),
      cache_(segment_mgr.get()),
      segment_mgr_(segment_mgr) {
  lru_head_.pre_ = nullptr;
  lru_head_.nxt_ = &lru_tail_;
  lru_tail_.pre_ = &lru_head_;
}

template <typename Application, typename Request, typename Response,
          typename ConnectionInfo, typename CacheKey, typename CacheEntry>
CacheInner<Application, Request, Response, ConnectionInfo, CacheKey,
           CacheEntry>::~CacheInner() {
  bip::offset_ptr<ListNode> node = lru_head_.nxt_;
  bip::offset_ptr<ListNode> nxt;

  while (node != &lru_tail_) {
    nxt = node->nxt_;
    segment_mgr_->destroy_ptr(node.get());
    node = nxt;
  }
}

template <typename Application, typename Request, typename Response,
          typename ConnectionInfo, typename CacheKey, typename CacheEntry>
bool CacheInner<Application, Request, Response, ConnectionInfo, CacheKey,
                CacheEntry>::Add(const CacheKey &key, const CacheEntry &value,
                                 const bool in_transaction,
                                 bip::offset_ptr<LogEntryInstance> dirty_node,
                                 bip::offset_ptr<CacheStateInstance>
                                     &new_state) {
  std::shared_lock<bip::interprocess_sharable_mutex> transaction_lock;
  if (!in_transaction) {
    transaction_lock =
        std::shared_lock<bip::interprocess_sharable_mutex>{transaction_mutex_};
  }

  bip::offset_ptr<ListNode> lru_node;
#define insert_entry(entry)                                       \
  new_state = entry->state;                                       \
  lru_node = entry->lru_node;                                     \
  if (!cache_.insert(std::make_pair(key, boost::move(*entry)))) { \
    entry->~MapEntry();                                           \
    segment_mgr_->destroy_ptr(entry);                             \
    return false;                                                 \
  }                                                               \
  segment_mgr_->destroy_ptr(entry)

  if constexpr (HasGetSize<CacheEntry>) {
    MapEntry *entry = segment_mgr_->template construct<MapEntry>(
        bip::anonymous_instance)(key, value, dirty_node, this, value.GetSize());
    insert_entry(entry);
  } else {
    MapEntry *entry = segment_mgr_->template construct<MapEntry>(
        bip::anonymous_instance)(key, value, dirty_node, this);
    insert_entry(entry);
  }
#undef insert_entry

  std::unique_lock<bip::interprocess_mutex> lru_lock(lru_mutex_);
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
  std::shared_lock<bip::interprocess_sharable_mutex> transaction_lock;
  if (!in_transaction) {
    transaction_lock =
        std::shared_lock<bip::interprocess_sharable_mutex>{transaction_mutex_};
  }
  return cache_.cvisit(key, [this, &value](auto &element) {
    value = element.second.state->value;

    std::unique_lock<bip::interprocess_mutex> lru_lock(lru_mutex_,
                                                       std::try_to_lock);
    if (lru_lock) {
      ListNode *lru_node = element.second.lru_node.get();
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
                                    bip::offset_ptr<LogEntryInstance>
                                        &dirty_node) {
  std::shared_lock<bip::interprocess_sharable_mutex> transaction_lock;
  if (!in_transaction) {
    transaction_lock =
        std::shared_lock<bip::interprocess_sharable_mutex>{transaction_mutex_};
  }
  bip::offset_ptr<ListNode> lru_node = nullptr;
  cache_.cvisit(key, [&](auto &element) {
    lru_node = element.second.lru_node;
    dirty_node = element.second.state->dirty_node;
  });
  if (!lru_node || !cache_.erase(key)) return false;

  std::unique_lock<bip::interprocess_mutex> lru_lock(lru_mutex_);
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
bool CacheInner<
    Application, Request, Response, ConnectionInfo, CacheKey,
    CacheEntry>::Replace(const CacheKey &key, const CacheEntry &value,
                         bool in_transaction,
                         bip::offset_ptr<LogEntryInstance> dirty_node,
                         std::mutex *logger_chr_mutex,
                         bip::offset_ptr<CacheStateInstance> &new_state) {
  std::shared_lock<bip::interprocess_sharable_mutex> transaction_lock;
  if (!in_transaction) {
    transaction_lock =
        std::shared_lock<bip::interprocess_sharable_mutex>{transaction_mutex_};
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
      segment_mgr_->destroy_ptr(old_dirty_node.get());
    }

    size_t new_size;
    if constexpr (HasGetSize<CacheEntry>) {
      new_size = value.GetSize();
    }
    ret = true;

    std::unique_lock<bip::interprocess_mutex> lru_lock(lru_mutex_,
                                                       std::try_to_lock);
    if (lru_lock) {
      bip::offset_ptr<ListNode> lru_node = element.second.lru_node;
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

    new_state = element.second.state;
  });

  if (size > max_size_) {
    std::unique_lock<bip::interprocess_mutex> lru_lock(lru_mutex_,
                                                       std::try_to_lock);
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
  std::shared_lock<bip::interprocess_sharable_mutex> transaction_lock;
  if (!in_transaction) {
    transaction_lock =
        std::shared_lock<bip::interprocess_sharable_mutex>{transaction_mutex_};
  }
  cache_.visit_all([&](auto &x) { visitor(x.first, x.second.state->value); });
}

template <typename Application, typename Request, typename Response,
          typename ConnectionInfo, typename CacheKey, typename CacheEntry>
void CacheInner<Application, Request, Response, ConnectionInfo, CacheKey,
                CacheEntry>::
    VisitAllState(std::function<void(CacheStateInstance *)> visitor,
                  bool in_transaction) {
  std::shared_lock<bip::interprocess_sharable_mutex> transaction_lock;
  if (!in_transaction) {
    transaction_lock =
        std::shared_lock<bip::interprocess_sharable_mutex>{transaction_mutex_};
  }
  cache_.visit_all([&](auto &x) { visitor(x.second.state.get()); });
}

template <typename Application, typename Request, typename Response,
          typename ConnectionInfo, typename CacheKey, typename CacheEntry>
void CacheInner<Application, Request, Response, ConnectionInfo, CacheKey,
                CacheEntry>::Evict() {
  if (emergency_mode_ptr_->load()) return;
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
      size -= moribund->state_->size;
    } else {
      size--;
    }

    cache_.erase(moribund->state_->key);
    segment_mgr_->destroy_ptr(moribund);
  }
  // LOG(INFO) << "Evict done: " << size << std::endl;
}

}  // namespace lite