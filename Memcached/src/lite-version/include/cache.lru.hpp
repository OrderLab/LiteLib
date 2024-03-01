#pragma once

#include <boost/unordered/concurrent_flat_map.hpp>
#include <iostream>
#include <mutex>
#include <memory>

namespace memcached {
namespace server {

struct CacheEntry {
  std::shared_ptr<std::vector<uint8_t>> value = nullptr;
  std::vector<uint8_t> flags;
  uint64_t CAS;
  uint64_t getSize() const { return value->capacity() + flags.capacity() + 1; }
};

class Cache {
 public:
  using TKey = std::vector<uint8_t>;

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

  bool Add(const TKey &key, const CacheEntry &value) {
    ListNode *node = new ListNode(key, value.getSize());
    if (!cache_.insert(std::make_pair(key, MapEntry(value, node)))) {
      delete node;
      return false;
    }

    std::unique_lock<std::mutex> lock(mutex_);
    PushFront(node);
    size_ += node->size_;
    if (size_ > max_size_) Evict();
    lock.unlock();

    return true;
  }

  bool Get(const TKey &key, CacheEntry &value) {
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

  bool Delete(const TKey &key) {
    ListNode *node = nullptr;
    cache_.cvisit(key,
                  [&node](auto &element) { node = element.second.list_node_; });
    if (!node || !cache_.erase(key)) return false;

    std::unique_lock<std::mutex> lock(mutex_, std::try_to_lock);
    if (lock) {
      Delink(node);
      size_ -= node->size_;
      delete node;
      lock.unlock();
    }
    return true;
  }

  bool Replace(const TKey &key, CacheEntry &value) {
    bool ret = false;
    cache_.visit(key, [this, &value, &ret](auto &element) {
      element.second.value_ = value;
      const auto new_size = value.getSize();
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
          size_ += new_size - node->size_;
          node->size_ = new_size;
          if (size_ > max_size_) Evict();
        }
        lock.unlock();
      }
    });
    return ret;
  }

 private:
  struct ListNode {
    TKey key_;
    size_t size_;
    ListNode *pre_, *nxt_;

    ListNode() : pre_(nullptr), nxt_(nullptr) {}

    explicit ListNode(const TKey &key, size_t size)
        : key_(key), size_(size), pre_(nullptr), nxt_(nullptr) {}

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
  size_t max_size_, size_;
  ListNode head_, tail_;
  boost::unordered::concurrent_flat_map<std::vector<uint8_t>, MapEntry> cache_;

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
      size_ -= moribund->size_;

      cache_.erase(moribund->key_);
      delete moribund;
    }
    // std::cerr << "Evict done: " << size_ << std::endl;
  }
};

}  // namespace server
}  // namespace memcached
