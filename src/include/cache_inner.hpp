// https://github.com/facebook/hhvm/blob/master/hphp/util/concurrent-lru-cache.h
#pragma once

#include <boost/interprocess/containers/pair.hpp>
#include <boost/interprocess/containers/string.hpp>
#include <boost/interprocess/sync/interprocess_mutex.hpp>
#include <boost/interprocess/sync/interprocess_sharable_mutex.hpp>
#include <boost/unordered/concurrent_flat_map.hpp>
#include <concepts>
#include <mutex>
#include <shared_mutex>

#include "concept.hpp"

namespace lite {

template <typename T>
concept HasGetSize = requires(T self) {
  { self.GetSize(self) } -> std::convertible_to<size_t>;
};

template <typename Application, typename Request, typename Response,
          typename ConnectionInfo, typename CacheKey, typename CacheEntry>
  requires IsProtocolMessage<Request>
class LogEntry;

template <typename Application, typename Request, typename Response,
          typename ConnectionInfo, typename CacheKey, typename CacheEntry>
struct CacheState {
  CacheKey key;
  CacheEntry value;
  std::conditional_t<HasGetSize<CacheEntry>, size_t, std::false_type> size;

  CacheState(bip::offset_ptr<SegmentManager> segment_mgr)
      : key(segment_mgr.get()), value(segment_mgr.get()) {}

  using LogEntryInstance = LogEntry<Application, Request, Response,
                                    ConnectionInfo, CacheKey, CacheEntry>;
  bip::offset_ptr<LogEntryInstance> dirty_node = nullptr;
};

template <typename Application, typename Request, typename Response,
          typename ConnectionInfo, typename CacheKey, typename CacheEntry>
class CacheInner {
  using LogEntryInstance = LogEntry<Application, Request, Response,
                                    ConnectionInfo, CacheKey, CacheEntry>;
  using CacheStateInstance = CacheState<Application, Request, Response,
                                        ConnectionInfo, CacheKey, CacheEntry>;

 public:
  bip::offset_ptr<SegmentManager> segment_mgr_;

  // If CacheEntry has GetSize method, then max_size_ is the sum of it.
  // Otherwise, max_size_ is the number of entries.
  explicit CacheInner(const size_t &max_size,
                      bip::offset_ptr<ShmAtomic<bool>> emergency_mode_ptr,
                      bip::offset_ptr<SegmentManager> segment_mgr);

  ~CacheInner();

  bool Add(const CacheKey &key, const CacheEntry &value,
           const bool in_transaction,
           bip::offset_ptr<LogEntryInstance> dirty_node,
           bip::offset_ptr<CacheStateInstance> &new_state);

  bool Get(const CacheKey &key, CacheEntry &value, bool in_transaction);

  bool Delete(const CacheKey &key, bool in_transaction,
              bip::offset_ptr<LogEntryInstance> &dirty_node);

  bool Replace(const CacheKey &key, const CacheEntry &value,
               bool in_transaction,
               bip::offset_ptr<LogEntryInstance> dirty_node,
               std::mutex *logger_chr_mutex,
               bip::offset_ptr<CacheStateInstance> &new_state);

  void ConstVisitAll(
      std::function<void(const CacheKey &, const CacheEntry &)> visitor,
      bool in_transaction);

  void VisitAllState(std::function<void(CacheStateInstance *)> visitor,
                     bool in_transaction);

  std::unique_lock<bip::interprocess_sharable_mutex> TransactionLock() {
    return std::unique_lock<bip::interprocess_sharable_mutex>{
        transaction_mutex_};
  }

  bip::offset_ptr<SegmentManager> GetSegmentManager() { return segment_mgr_; }

  bip::offset_ptr<ShmAtomic<bool>> emergency_mode_ptr_;

 private:
  class ListNode {
   public:
    CacheStateInstance *state_;

    ListNode *pre_ = nullptr, *nxt_ = nullptr;

    ListNode() : state_(nullptr) {}
    ListNode(CacheStateInstance *state) : state_(state) {}

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
    bip::offset_ptr<CacheInner> parent_;
    bip::offset_ptr<CacheStateInstance> state;
    bip::offset_ptr<ListNode> lru_node;

    MapEntry(const CacheKey &key, const CacheEntry &value,
             bip::offset_ptr<LogEntryInstance> dirty_node,
             bip::offset_ptr<CacheInner> parent, const size_t size = 0)
        : parent_(parent),
          state(parent_->segment_mgr_->template construct<CacheStateInstance>(
              bip::anonymous_instance)(parent_->segment_mgr_)) {
      state->key = key;
      state->value = value;
      state->dirty_node = dirty_node;
      if constexpr (HasGetSize<CacheEntry>) {
        state->size = size;
      }

      lru_node = parent_->segment_mgr_->template construct<ListNode>(
          bip::anonymous_instance)(state.get());
    }

    MapEntry(MapEntry &&other) noexcept
        : state(other.state), lru_node(other.lru_node), parent_(other.parent_) {
      other.state = nullptr;
      other.lru_node = nullptr;
    }

    ~MapEntry() {
      if (state) {
        parent_->segment_mgr_->destroy_ptr(state.get());
      }
      if (lru_node) {
        parent_->segment_mgr_->destroy_ptr(lru_node.get());
      }
    }
  };

  bip::interprocess_mutex lru_mutex_;
  ListNode lru_head_, lru_tail_;

  size_t max_size_, size;
  bip::interprocess_sharable_mutex transaction_mutex_;
  using MapAllocator = ShmAllocator<bip::pair<const CacheKey, MapEntry>>;
  boost::unordered::concurrent_flat_map<CacheKey, MapEntry,
                                        boost::hash<CacheKey>,
                                        std::equal_to<CacheKey>, MapAllocator>
      cache_;

  // WARNING: assumes that the mutex is held when calling this function.
  // TODO: how to notify application
  void Evict();
};

}  // namespace lite