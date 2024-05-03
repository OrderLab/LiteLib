// https://github.com/facebook/hhvm/blob/master/hphp/util/concurrent-lru-cache.h
#pragma once

#include <boost/unordered/concurrent_flat_map.hpp>
#include <concepts>
#include <iostream>
#include <mutex>
#include <shared_mutex>

namespace lite {

template <typename T>
concept HasGetSize = requires(T self) {
  { self.GetSize(self) } -> std::convertible_to<size_t>;
};

template <typename Key, typename CacheEntry>
class CacheInner {
 public:
  // If CacheEntry has GetSize method, then max_size_ is the sum of it.
  // Otherwise, max_size_ is the number of entries.
  explicit CacheInner(const size_t &max_size, std::atomic<bool> &emergency_mode)
      : max_size_(max_size), size(0), emergency_mode_(emergency_mode) {
    lru_head_.pre_ = nullptr;
    lru_head_.nxt_ = &lru_tail_;
    lru_tail_.pre_ = &lru_head_;
  }

  ~CacheInner() {
    ListNode *node = lru_head_.nxt_;
    ListNode *nxt;

    while (node != &lru_tail_) {
      nxt = node->nxt_;
      delete node;
      node = nxt;
    }
  }

  struct State {
    Key key;
    CacheEntry value;
    std::conditional_t<HasGetSize<CacheEntry>, size_t, std::false_type> size;

    void *dirty_node = nullptr;  // LogEntry *
  };

  bool Add(const Key &key, const CacheEntry &value, const bool in_transaction,
           void *dirty_node, State *&new_state) {
    std::shared_lock<std::shared_mutex> transaction_lock;
    if (!in_transaction) {
      transaction_lock =
          std::shared_lock<std::shared_mutex>{transaction_mutex_};
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

  bool Get(const Key &key, CacheEntry &value, bool in_transaction) {
    std::shared_lock<std::shared_mutex> transaction_lock;
    if (!in_transaction) {
      transaction_lock =
          std::shared_lock<std::shared_mutex>{transaction_mutex_};
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

  bool Delete(const Key &key, bool in_transaction, void *&dirty_node) {
    std::shared_lock<std::shared_mutex> transaction_lock;
    if (!in_transaction) {
      transaction_lock =
          std::shared_lock<std::shared_mutex>{transaction_mutex_};
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

  bool Replace(const Key &key, const CacheEntry &value, bool in_transaction,
               void *dirty_node, void *&old_dirty_node, State *&new_state) {
    std::shared_lock<std::shared_mutex> transaction_lock;
    if (!in_transaction) {
      transaction_lock =
          std::shared_lock<std::shared_mutex>{transaction_mutex_};
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

  void ConstVisitAll(
      std::function<void(const Key &, const CacheEntry &)> visitor,
      bool in_transaction) {
    std::shared_lock<std::shared_mutex> transaction_lock;
    if (!in_transaction) {
      transaction_lock =
          std::shared_lock<std::shared_mutex>{transaction_mutex_};
    }
    cache_.visit_all([&](auto &x) { visitor(x.first, x.second.value); });
  }

  std::unique_lock<std::shared_mutex> TransactionLock() {
    return std::unique_lock<std::shared_mutex>{transaction_mutex_};
  }

  std::atomic<bool> &emergency_mode_;

 private:
  class ListNode {
   public:
    State *state_;

    ListNode *pre_ = nullptr, *nxt_ = nullptr;

    ListNode() : state_(nullptr) {}
    ListNode(State *state) : state_(state) {}

    bool isInList() const { return pre_ != nullptr; }

    void Delink() {
      pre_->nxt_ = nxt_;
      nxt_->pre_ = pre_;
      pre_ = nullptr;
    }

    // Push a delinked node to the front
    void PushFront(ListNode &head) {
      pre_ = &head;
      nxt_ = head.nxt_;
      head.nxt_->pre_ = this;
      head.nxt_ = this;
    }
  };

  struct MapEntry {
    std::unique_ptr<State>
        state;  // use unique_ptr to accelerate move constructor

    ListNode *lru_node;

    MapEntry(const Key &key, const CacheEntry &value, void *dirty_node,
             const size_t size = 0)
        : state(std::make_unique<State>()) {
      state->key = key;
      state->value = value;
      state->dirty_node = dirty_node;
      if constexpr (HasGetSize<CacheEntry>) {
        state->size = size;
      }
      lru_node = new ListNode(state.get());
    }
  };

  std::mutex lru_mutex_;
  ListNode lru_head_, lru_tail_;

  size_t max_size_, size;
  std::shared_mutex transaction_mutex_;
  boost::unordered::concurrent_flat_map<Key, MapEntry> cache_;

  // WARNING: assumes that the mutex is held when calling this function.
  // TODO: how to notify application
  void Evict() {
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
};

}  // namespace lite