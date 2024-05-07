#pragma once

#include "cache_inner.hpp"

namespace lite {

template <typename Key, typename CacheEntry>
CacheInner<Key, CacheEntry>::CacheInner(const size_t &max_size,
                                        std::atomic<bool> &emergency_mode)
    : max_size_(max_size), size(0), emergency_mode_(emergency_mode) {
  lru_head_.pre_ = nullptr;
  lru_head_.nxt_ = &lru_tail_;
  lru_tail_.pre_ = &lru_head_;
}

template <typename Key, typename CacheEntry>
CacheInner<Key, CacheEntry>::~CacheInner() {
  ListNode *node = lru_head_.nxt_;
  ListNode *nxt;

  while (node != &lru_tail_) {
    nxt = node->nxt_;
    delete node;
    node = nxt;
  }
}

template <typename Key, typename CacheEntry>
bool CacheInner<Key, CacheEntry>::Add(const Key &key, const CacheEntry &value,
                                      const bool in_transaction,
                                      void *dirty_node, State *&new_state) {
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

template <typename Key, typename CacheEntry>
bool CacheInner<Key, CacheEntry>::Get(const Key &key, CacheEntry &value,
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

template <typename Key, typename CacheEntry>
bool CacheInner<Key, CacheEntry>::Delete(const Key &key, bool in_transaction,
                                         void *&dirty_node) {
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

template <typename Key, typename CacheEntry>
bool CacheInner<Key, CacheEntry>::Replace(const Key &key,
                                          const CacheEntry &value,
                                          bool in_transaction, void *dirty_node,
                                          void *&old_dirty_node,
                                          State *&new_state) {
  std::shared_lock<std::shared_mutex> transaction_lock;
  if (!in_transaction) {
    transaction_lock = std::shared_lock<std::shared_mutex>{transaction_mutex_};
  }
  bool ret = false;
  cache_.visit(key, [&](auto &element) {
    element.second.state->value = value;
    old_dirty_node = element.second.state->dirty_node;
    element.second.state->dirty_node = dirty_node;

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
        if (size > max_size_) Evict();
      }
      lru_lock.unlock();
    }

    new_state = element.second.state.get();
  });
  return ret;
}

template <typename Key, typename CacheEntry>
void CacheInner<Key, CacheEntry>::ConstVisitAll(
    std::function<void(const Key &, const CacheEntry &)> visitor,
    bool in_transaction) {
  std::shared_lock<std::shared_mutex> transaction_lock;
  if (!in_transaction) {
    transaction_lock = std::shared_lock<std::shared_mutex>{transaction_mutex_};
  }
  cache_.visit_all([&](auto &x) { visitor(x.first, x.second.value); });
}

template <typename Key, typename CacheEntry>
void CacheInner<Key, CacheEntry>::Evict() {
  // std::cerr << "Evict" << std::endl;
  while (size > max_size_) {
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
  // std::cerr << "Evict done: " << size << std::endl;
}

}  // namespace lite