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
  explicit CacheInner(const size_t &max_size,
                      std::atomic<bool> &emergency_mode);

  ~CacheInner();

  struct State {
    Key key;
    CacheEntry value;
    std::conditional_t<HasGetSize<CacheEntry>, size_t, std::false_type> size;

    void *dirty_node = nullptr;  // LogEntry *
  };

  bool Add(const Key &key, const CacheEntry &value, const bool in_transaction,
           void *dirty_node, State *&new_state);

  bool Get(const Key &key, CacheEntry &value, bool in_transaction);

  bool Delete(const Key &key, bool in_transaction, void *&dirty_node);

  bool Replace(const Key &key, const CacheEntry &value, bool in_transaction,
               void *dirty_node, void *&old_dirty_node, State *&new_state);

  void ConstVisitAll(
      std::function<void(const Key &, const CacheEntry &)> visitor,
      bool in_transaction);

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
  void Evict();
};

}  // namespace lite