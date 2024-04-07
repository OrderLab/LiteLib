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
class Cache {
 public:
  // If CacheEntry has GetSize method, then max_size_ is the sum of it.
  // Otherwise, max_size_ is the number of entries.
  explicit Cache(const size_t &max_size) : max_size_(max_size), size_(0) {
    head_.pre_ = nullptr;
    head_.nxt_ = &tail_;
    tail_.pre_ = &head_;
  }

  ~Cache() {
    ListNode *node = head_.nxt_;
    ListNode *nxt;
    while (node != &tail_) {
      nxt = node->nxt_;
      delete node;
      node = nxt;
    }
    head_.nxt_ = &tail_;
    tail_.pre_ = &head_;
  }

  bool Add(const Key &key, const CacheEntry &value,
           bool in_transaction = false) {
    std::shared_lock<std::shared_mutex> transaction_lock;
    if (!in_transaction) {
      transaction_lock =
          std::shared_lock<std::shared_mutex>{transaction_mutex_};
    }
    ListNode *node = new ListNode(key, value.GetSize());
    if (!cache_.insert(std::make_pair(key, MapEntry(value, node)))) {
      delete node;
      return false;
    }

    std::unique_lock<std::mutex> lock(mutex_);
    PushFront(node);
    if constexpr (HasGetSize<CacheEntry>) {
      size_ += node->size_;
    } else {
      size_++;
    }
    if (size_ > max_size_) Evict();
    lock.unlock();

    return true;
  }

  bool Get(const Key &key, CacheEntry &value, bool in_transaction = false) {
    std::shared_lock<std::shared_mutex> transaction_lock;
    if (!in_transaction) {
      transaction_lock =
          std::shared_lock<std::shared_mutex>{transaction_mutex_};
    }
    return cache_.cvisit(key, [this, &value](auto &element) {
      value = element.second.value_;

      std::unique_lock<std::mutex> lock(mutex_, std::try_to_lock);
      if (lock) {
        ListNode *node = element.second.list_node_;
        // The list node may be out of the list if it is in the process of being
        // inserted or evicted. Doing this check allows us to lock the list for
        // shorter periods of time.
        if (node->isInList()) {
          Delink(node);
          PushFront(node);
        }
        lock.unlock();
      }
    });
  }

  bool Delete(const Key &key, bool in_transaction = false) {
    std::shared_lock<std::shared_mutex> transaction_lock;
    if (!in_transaction) {
      transaction_lock =
          std::shared_lock<std::shared_mutex>{transaction_mutex_};
    }
    ListNode *node = nullptr;
    cache_.cvisit(key,
                  [&node](auto &element) { node = element.second.list_node_; });
    if (!node || !cache_.erase(key)) return false;

    std::unique_lock<std::mutex> lock(mutex_, std::try_to_lock);
    if (lock) {
      Delink(node);
      if constexpr (HasGetSize<CacheEntry>) {
        size_ -= node->size_;
      } else {
        size_--;
      }
      delete node;
      lock.unlock();
    }
    return true;
  }

  bool Replace(const Key &key, const CacheEntry &value,
               bool in_transaction = false) {
    std::shared_lock<std::shared_mutex> transaction_lock;
    if (!in_transaction) {
      transaction_lock =
          std::shared_lock<std::shared_mutex>{transaction_mutex_};
    }
    bool ret = false;
    cache_.visit(key, [this, &value, &ret](auto &element) {
      element.second.value_ = value;
      size_t new_size;
      if constexpr (HasGetSize<CacheEntry>) {
        new_size = value.GetSize();
      }
      ret = true;

      std::unique_lock<std::mutex> lock(mutex_, std::try_to_lock);
      if (lock) {
        ListNode *node = element.second.list_node_;
        // The list node may be out of the list if it is in the process of being
        // inserted or evicted. Doing this check allows us to lock the list for
        // shorter periods of time.
        if (node->isInList()) {
          Delink(node);
          PushFront(node);
          if constexpr (HasGetSize<CacheEntry>) {
            size_ += new_size - node->size_;
            node->size_ = new_size;
          }
          if (size_ > max_size_) Evict();
        }
        lock.unlock();
      }
    });
    return ret;
  }

  bool Set(const Key &key, const CacheEntry &value,
           bool in_transaction = false) {
    std::shared_lock<std::shared_mutex> transaction_lock;
    if (!in_transaction) {
      transaction_lock =
          std::shared_lock<std::shared_mutex>{transaction_mutex_};
    }
    if (Add(key, value, in_transaction)) return true;
    return Replace(key, value, in_transaction);
  }

  void ConstVisitAll(
      std::function<void(const Key &, const CacheEntry &)> visitor,
      bool in_transaction = false) {
    std::shared_lock<std::shared_mutex> transaction_lock;
    if (!in_transaction) {
      transaction_lock =
          std::shared_lock<std::shared_mutex>{transaction_mutex_};
    }
    cache_.visit_all([&](auto &x) { visitor(x.first, x.second.value_); });
  }

  std::unique_lock<std::shared_mutex> TransactionLock() {
    return std::unique_lock<std::shared_mutex>{transaction_mutex_};
  }

 private:
  struct ListNode {
    Key key_;
    std::conditional_t<HasGetSize<CacheEntry>, size_t, std::false_type> size_;
    ListNode *pre_, *nxt_;

    ListNode() : pre_(nullptr), nxt_(nullptr) {}

    explicit ListNode(const Key &key, size_t size = 0)
        : key_(key), pre_(nullptr), nxt_(nullptr) {
      if constexpr (HasGetSize<CacheEntry>) {
        size_ = size;
      }
    }

    bool isInList() const { return pre_ != nullptr; }
  };

  void Delink(ListNode *node) {
    ListNode *prev = node->pre_;
    ListNode *nxt = node->nxt_;
    prev->nxt_ = nxt;
    nxt->pre_ = prev;
    node->pre_ = nullptr;
  }
  void PushFront(ListNode *node) {
    ListNode *oldRealHead = head_.nxt_;
    node->pre_ = &head_;
    node->nxt_ = oldRealHead;
    oldRealHead->pre_ = node;
    head_.nxt_ = node;
  }

  struct MapEntry {
    CacheEntry value_;
    ListNode *list_node_;

    MapEntry() : list_node_(nullptr) {}

    MapEntry(const CacheEntry &value, ListNode *const list_node)
        : value_(value), list_node_(list_node) {}
  };

  std::mutex mutex_;
  std::shared_mutex transaction_mutex_;
  size_t max_size_, size_;
  ListNode head_, tail_;
  boost::unordered::concurrent_flat_map<Key, MapEntry> cache_;

  // WARNING: assumes that the mutex is held when calling this function.
  void Evict() {
    // std::cerr << "Evict" << std::endl;
    while (size_ > max_size_) {
      ListNode *moribund = tail_.pre_;
      if (moribund == &head_) {
        // List is empty, can't evict
        return;
      }
      Delink(moribund);
      if constexpr (HasGetSize<CacheEntry>) {
        size_ -= moribund->size_;
      } else {
        size_--;
      }

      cache_.erase(moribund->key_);
      delete moribund;
    }
    // std::cerr << "Evict done: " << size_ << std::endl;
  }
};

}  // namespace lite