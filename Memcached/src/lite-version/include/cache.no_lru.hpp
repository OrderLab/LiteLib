#pragma once

#include <boost/unordered/concurrent_flat_map.hpp>

namespace memcached {
namespace server {

struct CacheEntry {
  std::vector<uint8_t> value;
  std::vector<uint8_t> flags;
  uint64_t CAS;
};

class Cache {
 public:
  Cache() = default;
  ~Cache() = default;

  bool Add(const std::vector<uint8_t> &key, const CacheEntry &value) {
    return cache_.insert(std::make_pair(key, value));
  }

  bool Set(const std::vector<uint8_t> &key, const CacheEntry &value) {
    cache_.insert_or_assign(key, value);
    return true;
  }

  bool Get(const std::vector<uint8_t> &key, CacheEntry &value) {
    return cache_.cvisit(key,
                         [&value](auto &element) { value = element.second; });
  }

  bool Delete(const std::vector<uint8_t> &key) { return cache_.erase(key); }

  bool Replace(const std::vector<uint8_t> &key, CacheEntry &value) {
    bool ret = false;
    cache_.visit(key, [&value, &ret](auto &element) {
      element.second = value;
      ret = true;
    });
    return ret;
  }

 private:
  boost::unordered::concurrent_flat_map<std::vector<uint8_t>, CacheEntry>
      cache_;
};

}  // namespace server
}  // namespace memcached
