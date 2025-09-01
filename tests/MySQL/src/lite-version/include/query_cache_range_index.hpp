#pragma once

#include <lite.hpp>

#include "dissect.hpp"

class QueryAndResult;

// TODO: support more value types
class QueryCacheRangeIndex {
 public:
  struct Node {
    std::unique_ptr<std::shared_mutex> mutex_ptr;
    std::set<std::shared_ptr<QueryAndResult>> query_and_results;

    Node() : mutex_ptr(std::make_unique<std::shared_mutex>()) {}
    Node(std::shared_ptr<QueryAndResult> query_and_result)
        : mutex_ptr(std::make_unique<std::shared_mutex>()) {
      query_and_results.insert(query_and_result);
    }
    Node(Node &&rhs)
        : query_and_results(std::move(rhs.query_and_results)),
          mutex_ptr(std::move(rhs.mutex_ptr)) {}
  };

  std::vector<std::shared_ptr<QueryAndResult>> Query(const int index);

  void Insert(std::shared_ptr<QueryAndResult> query_and_result, const int begin, const int end);

  void Delete(std::shared_ptr<QueryAndResult> query_and_result);

  void Delete(std::shared_ptr<QueryAndResult> query_and_result, const int begin, const int end);

 private:
  static const int min = std::numeric_limits<int>::min();
  static const int max = std::numeric_limits<int>::max();

  struct Range {
    int begin, end;  // inclusive, children [begin, mid], [mid + 1, end]
    friend std::size_t hash_value(const Range &self) {
      std::size_t seed = 0;
      boost::hash_combine(seed, self.begin);
      boost::hash_combine(seed, self.end);
      return seed;
    }
    bool operator==(const Range &rhs) const {
      return begin == rhs.begin && end == rhs.end;
    }
  };

  boost::unordered::concurrent_flat_map<Range, Node> range_index;

  void InsertInternal(std::shared_ptr<QueryAndResult> query_and_result, const int begin,
                      const int end, const int node_begin, const int node_end);

  void DeleteInternal(std::shared_ptr<QueryAndResult> query_and_result, const int begin,
                      const int end, const int node_begin, const int node_end);
};