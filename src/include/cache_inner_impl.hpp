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
                                   ShmVoidAllocator allocator)
    : max_size_(max_size),
      size(0),
      emergency_mode_ptr_(emergency_mode_ptr),
      allocator_(allocator),
      list_node_allocator_(allocator),
      log_entry_allocator_(allocator),
      cache_state_allocator_(allocator),
      cache_(allocator) {
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
    node->~ListNode();
    list_node_allocator_.deallocate_one(node);
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
  // bip::sharable_lock<bip::interprocess_sharable_mutex> transaction_lock;
  // if (!in_transaction) {
  //   transaction_lock = bip::sharable_lock<bip::interprocess_sharable_mutex>(
  //       transaction_mutex_);
  // }

  auto item_size = 0;
  if constexpr (HasGetSize<CacheEntry>) {
    item_size = value.GetSize();
  }

  ListNode *lru_node = nullptr;
  auto ret = cache_.emplace_and_visit(
      std::piecewise_construct, std::forward_as_tuple(key),
      std::forward_as_tuple(key, value, dirty_node, this, item_size),
      [&](auto &element) {
        new_state = element.second.state;
        lru_node = element.second.lru_node.get();
      },
      [&](auto &element) {});

  if (lru_node) {
    // bip::scoped_lock<bip::interprocess_mutex> lru_lock(lru_mutex_);
    lru_node->PushFront(lru_head_);
    if constexpr (HasGetSize<CacheEntry>) {
      size += lru_node->state_->size;
    } else {
      size++;
    }
    if (size > max_size_) Evict();
    // lru_lock.unlock();
  }

  return ret;
}

template <typename Application, typename Request, typename Response,
          typename ConnectionInfo, typename CacheKey, typename CacheEntry>
bool CacheInner<Application, Request, Response, ConnectionInfo, CacheKey,
                CacheEntry>::Get(const CacheKey &key, CacheEntry &value,
                                 bool in_transaction) {
  // bip::sharable_lock<bip::interprocess_sharable_mutex> transaction_lock;
  // if (!in_transaction) {
  //   transaction_lock = bip::sharable_lock<bip::interprocess_sharable_mutex>(
  //       transaction_mutex_);
  // }

  ListNode *lru_node = nullptr;
  auto ret = cache_.cvisit(key, [this, &value, &lru_node](auto &element) {
    value = element.second.state->value;
    lru_node = element.second.lru_node.get();
  });

  if (lru_node) {
    // bip::scoped_lock<bip::interprocess_mutex> lru_lock(lru_mutex_,
    //                                                    bip::try_to_lock);
    // if (lru_lock) {
    // The list node may be out of the list if it is in the process of being
    // inserted or evicted. Doing this check allows us to lock the list for
    // shorter periods of time.
    if (lru_node->isInList()) {
      lru_node->Delink();
      lru_node->PushFront(lru_head_);
    }
    //   lru_lock.unlock();
    // }
  }

  return ret;
}

template <typename Application, typename Request, typename Response,
          typename ConnectionInfo, typename CacheKey, typename CacheEntry>
bool CacheInner<Application, Request, Response, ConnectionInfo, CacheKey,
                CacheEntry>::Delete(const CacheKey &key, bool in_transaction,
                                    bip::offset_ptr<LogEntryInstance>
                                        &dirty_node) {
  // bip::sharable_lock<bip::interprocess_sharable_mutex> transaction_lock;
  // if (!in_transaction) {
  //   transaction_lock = bip::sharable_lock<bip::interprocess_sharable_mutex>(
  //       transaction_mutex_);
  // }

  bip::offset_ptr<ListNode> lru_node = nullptr;
  cache_.cvisit(key, [&](auto &element) {
    lru_node = element.second.lru_node;
    dirty_node = element.second.state->dirty_node;
  });
  if (!lru_node || !cache_.erase(key)) return false;

  // bip::scoped_lock<bip::interprocess_mutex> lru_lock(lru_mutex_);
  lru_node->Delink();
  if constexpr (HasGetSize<CacheEntry>) {
    size -= lru_node->state_->size;
  } else {
    size--;
  }
  // lru_lock.unlock();
  lru_node->~ListNode();
  list_node_allocator_.deallocate_one(lru_node);

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
  // bip::sharable_lock<bip::interprocess_sharable_mutex> transaction_lock;
  // if (!in_transaction) {
  //   transaction_lock = bip::sharable_lock<bip::interprocess_sharable_mutex>(
  //       transaction_mutex_);
  // }
  bool ret = false;
  ListNode *lru_node = nullptr;
  size_t new_size;

  cache_.visit(key, [&](auto &element) {
    element.second.state->value = value;

    bip::scoped_lock<std::mutex> chr_lock;
    if (logger_chr_mutex) {
      chr_lock = bip::scoped_lock<std::mutex>{*logger_chr_mutex};
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
      old_dirty_node->~LogEntryInstance();
      log_entry_allocator_.deallocate_one(old_dirty_node);
    }

    if constexpr (HasGetSize<CacheEntry>) {
      new_size = value.GetSize();
    }
    ret = true;

    new_state = element.second.state;
    lru_node = element.second.lru_node.get();
  });

  if (lru_node) {
    // bip::scoped_lock<bip::interprocess_mutex> lru_lock(lru_mutex_,
    //                                                    bip::try_to_lock);
    // if (lru_lock) {
    // The list node may be out of the list if it is in the process of being
    // inserted or evicted. Doing this check allows us to lock the list for
    // shorter periods of time.
    if (lru_node->isInList()) {
      lru_node->Delink();
      lru_node->PushFront(lru_head_);
      if constexpr (HasGetSize<CacheEntry>) {
        size += new_size - lru_node->size;
        lru_node->size = new_size;
        if (size > max_size_) Evict();
      }
    }
    //   lru_lock.unlock();
    // }
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
  // bip::sharable_lock<bip::interprocess_sharable_mutex> transaction_lock;
  // if (!in_transaction) {
  //   transaction_lock = bip::sharable_lock<bip::interprocess_sharable_mutex>(
  //       transaction_mutex_);
  // }
  cache_.visit_all([&](auto &x) { visitor(x.first, x.second.state->value); });
}

template <typename Application, typename Request, typename Response,
          typename ConnectionInfo, typename CacheKey, typename CacheEntry>
void CacheInner<Application, Request, Response, ConnectionInfo, CacheKey,
                CacheEntry>::
    VisitAllState(std::function<void(CacheStateInstance *)> visitor,
                  bool in_transaction) {
  // bip::sharable_lock<bip::interprocess_sharable_mutex> transaction_lock;
  // if (!in_transaction) {
  //   transaction_lock = bip::sharable_lock<bip::interprocess_sharable_mutex>(
  //       transaction_mutex_);
  // }
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
    bip::offset_ptr<ListNode> moribund = lru_tail_.pre_;
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
  }
  // LOG(INFO) << "Evict done: " << size << std::endl;
}

}  // namespace lite