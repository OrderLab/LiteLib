#pragma once

#include <boost/functional/hash.hpp>
#include <mutex>
#include <vector>
#include <memory>

namespace memcached {
namespace server {

struct CacheEntry {
  std::shared_ptr<std::vector<uint8_t>> value = nullptr;
  std::vector<uint8_t> flags;
  uint64_t CAS;
};

class Cache {
 public:
  using TKey = std::vector<uint8_t>;

  explicit Cache(const size_t &max_size) : max_size_(max_size) {
    array_ = new ArrayEntry *[max_size];
  }

  ~Cache() {
    delete [] array_;
  }

  bool Add(const TKey &key, const CacheEntry &value) {
    auto id = hasher(key) % max_size_;
    std::unique_lock<std::mutex> lock(mutex_);
    if (array_[id]) delete array_[id];
    array_[id] = new ArrayEntry{key, value};
    return true;
  }

  bool Get(const TKey &key, CacheEntry &value) {
    auto id = hasher(key) % max_size_;
    std::unique_lock<std::mutex> lock(mutex_);
    if (array_[id] && array_[id]->key == key) {
      value = array_[id]->value;
      return true;
    }
    return false;
  }

  bool Delete(const TKey &key) {
    auto id = hasher(key) % max_size_;
    std::unique_lock<std::mutex> lock(mutex_);
    if (array_[id] && array_[id]->key == key) {
      delete array_[id];
      return true;
    }
    return false;
  }

  bool Replace(const TKey &key, CacheEntry &value) {
    auto id = hasher(key) % max_size_;
    std::unique_lock<std::mutex> lock(mutex_);
    if (array_[id] && array_[id]->key == key) {
      array_[id] = new ArrayEntry{key, value};
      return true;
    }
    return false;
  }

 private:
  struct ArrayEntry {
    TKey key;
    CacheEntry value;
  };
  std::mutex mutex_;
  boost::hash<TKey> hasher;
  size_t max_size_;
  ArrayEntry **array_;
};

}  // namespace server
}  // namespace memcached
