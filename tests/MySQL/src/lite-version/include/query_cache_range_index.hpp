#pragma once

#include <lite.hpp>

#include "dissect.hpp"

class QueryAndResult;

// TODO: support more value types
class QueryCacheRangeIndex {
 public:
  struct Node {
    std::shared_mutex mutex;
    std::set<QueryAndResult *> query_and_results;

    Node() = default;
    Node(QueryAndResult *query_and_result) {
      query_and_results.insert(query_and_result);
    }
    Node(Node &&rhs)
        : query_and_results(std::move(rhs.query_and_results)) {
    }  // TODO: make sure mutex is correct
  };

  std::vector<QueryAndResult *> Query(const int index);

  void Insert(QueryAndResult *query_and_result, const int begin, const int end);

  void Delete(QueryAndResult *query_and_result);

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

  void InsertInternal(QueryAndResult *query_and_result, const int begin,
                      const int end, const int node_begin, const int node_end);

  void DeleteInternal(QueryAndResult *query_and_result, const int begin,
                      const int end, const int node_begin, const int node_end);
};