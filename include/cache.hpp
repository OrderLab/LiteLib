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
  explicit Cache(const size_t &max_size, std::atomic<bool> &emergency_mode)
      : max_size_(max_size),
        size(0),
        emergency_mode_(emergency_mode),
        dirties(dirty_mutex_, dirty_head_, dirty_tail_) {
    lru_head_.pre_ = nullptr;
    lru_head_.nxt_ = &lru_tail_;
    lru_tail_.pre_ = &lru_head_;
    dirty_head_.pre_ = nullptr;
    dirty_head_.nxt_ = &dirty_tail_;
    dirty_tail_.pre_ = &dirty_head_;
  }

  ~Cache() {
    ListNode *node = lru_head_.nxt_;
    ListNode *nxt;

    while (node != &lru_tail_) {
      nxt = node->nxt_;
      delete node;
      node = nxt;
    }

    node = dirty_head_.nxt_;
    while (node != &dirty_tail_) {
      nxt = node->nxt_;
      delete node;
      node = nxt;
    }
  }

  bool Add(const Key &key, const CacheEntry &value,
           bool in_transaction = false) {
    std::shared_lock<std::shared_mutex> transaction_lock;
    if (!in_transaction) {
      transaction_lock =
          std::shared_lock<std::shared_mutex>{transaction_mutex_};
    }
    ListNode *lru_node = new ListNode;
    MapEntry entry = MapEntry(key, value, lru_node,
                              HasGetSize<CacheEntry> ? value.GetSize() : 0);
    std::unique_ptr<ListNode> &dirty_node = entry.dirty_node;
    if (!cache_.insert(std::make_pair(key, std::move(entry)))) {
      delete lru_node;
      return false;
    }

    std::unique_lock<std::mutex> lru_lock(lru_mutex_);
    lru_node->PushFront(lru_head_);
    if constexpr (HasGetSize<CacheEntry>) {
      size += lru_node->data_->size;
    } else {
      size++;
    }
    if (size > max_size_) Evict();
    lru_lock.unlock();

    if (emergency_mode_) {
      std::unique_lock<std::mutex> dirty_lock(dirty_mutex_);
      dirty_node->PushFront(dirty_head_);
    }

    return true;
  }

  bool Get(const Key &key, CacheEntry &value, bool in_transaction = false) {
    std::shared_lock<std::shared_mutex> transaction_lock;
    if (!in_transaction) {
      transaction_lock =
          std::shared_lock<std::shared_mutex>{transaction_mutex_};
    }
    return cache_.cvisit(key, [this, &value](auto &element) {
      value = element.second.data->value;

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

  // TODO: how to force the application to log it?
  bool Delete(const Key &key, bool in_transaction = false) {
    std::shared_lock<std::shared_mutex> transaction_lock;
    if (!in_transaction) {
      transaction_lock =
          std::shared_lock<std::shared_mutex>{transaction_mutex_};
    }
    ListNode *lru_node = nullptr;
    cache_.cvisit(key, [&lru_node](auto &element) {
      lru_node = element.second.lru_node;
    });
    if (!lru_node || !cache_.erase(key)) return false;

    std::unique_lock<std::mutex> lru_lock(lru_mutex_);
    lru_node->Delink();
    if constexpr (HasGetSize<CacheEntry>) {
      size -= lru_node->data_->size;
    } else {
      size--;
    }
    lru_lock.unlock();
    delete lru_node;

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
      element.second.data->value = value;
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

      ListNode *dirty_node = element.second.dirty_node.get();
      if (emergency_mode_ && !dirty_node->isInList()) {
        std::unique_lock<std::mutex> dirty_lock(dirty_mutex_);
        if (!dirty_node->isInList()) dirty_node->PushFront(dirty_head_);
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
    cache_.visit_all([&](auto &x) { visitor(x.first, x.second.value); });
  }

  std::unique_lock<std::shared_mutex> TransactionLock() {
    return std::unique_lock<std::shared_mutex>{transaction_mutex_};
  }

 private:
  struct Data {
    Key key;
    CacheEntry value;
    std::conditional_t<HasGetSize<CacheEntry>, size_t, std::false_type> size;
  };

  class ListNode {
   public:
    Data *data_;

    ListNode *pre_ = nullptr, *nxt_ = nullptr;

    ListNode() : data_(nullptr) {}
    ListNode(Data *data) : data_(data) {}

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
    std::unique_ptr<Data> data;

    ListNode *lru_node;
    std::unique_ptr<ListNode> dirty_node;

    MapEntry(const Key &key, const CacheEntry &value, ListNode *const list_node,
             const size_t size = 0)
        : data(std::make_unique<Data>()), lru_node(list_node) {
      data->key = key;
      data->value = value;
      if constexpr (HasGetSize<CacheEntry>) {
        data->size = size;
      }
      dirty_node = std::make_unique<ListNode>(data.get());
    }
  };

  std::mutex lru_mutex_;
  ListNode lru_head_, lru_tail_;

  std::mutex dirty_mutex_;
  ListNode dirty_head_, dirty_tail_;

  std::atomic<bool> &emergency_mode_;
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

      cache_.erase(moribund->data_->key);
      delete moribund;
    }
    // std::cerr << "Evict done: " << size << std::endl;
  }

 public:
  class Dirties {
   public:
    class Iterator {
     public:
      using difference_type = std::ptrdiff_t;

      Iterator(ListNode *ptr, std::mutex &dirty_mutex_)
          : ptr_(ptr), dirty_mutex_(dirty_mutex_) {}

      std::pair<Key, CacheEntry> operator*() const {
        return std::make_pair(ptr_->data_->key, ptr_->data_->value);
      }
      Iterator &operator++() {
        const auto old_ptr = ptr_;
        ptr_ = ptr_->nxt_;
        std::unique_lock<std::mutex> dirty_lock(dirty_mutex_);
        old_ptr->Delink();
        return *this;
      }
      bool operator!=(const Iterator &rhs) const { return ptr_ != rhs.ptr_; }

     private:
      ListNode *ptr_;
      std::mutex &dirty_mutex_;
    };

    Iterator begin() { return Iterator(dirty_tail_.pre_, dirty_mutex_); }
    Iterator end() { return Iterator(&dirty_head_, dirty_mutex_); }
    bool Empty() { return dirty_tail_.pre_ == &dirty_head_; }

    Dirties(std::mutex &dirty_mutex_, ListNode &dirty_head_,
            ListNode &dirty_tail)
        : dirty_mutex_(dirty_mutex_),
          dirty_head_(dirty_head_),
          dirty_tail_(dirty_tail) {}

   private:
    std::mutex &dirty_mutex_;
    ListNode &dirty_head_, &dirty_tail_;
  } dirties;
};

}  // namespace lite