#include "query_cache_range_index.hpp"

#include "query_cache.hpp"

std::vector<QueryAndResult *> QueryCacheRangeIndex::Query(const int index) {
  std::vector<QueryAndResult *> ret;
  int begin = min, end = max;
  for (;;) {
    range_index.visit(Range{begin, end}, [&](auto &node_it) {
      auto &[_, node] = node_it;
      std::shared_lock node_lock(*node.mutex_ptr);
      for (auto query_and_result : node.query_and_results) {
        ret.push_back(query_and_result);
      }
    });
    if (begin == end) break;
    long long mid = ((long long)begin + (long long)end) / 2;
    if (mid <= index) {
      begin = mid + 1;
    } else {
      end = mid;
    }
  };
  return ret;
}

void QueryCacheRangeIndex::Insert(QueryAndResult *query_and_result,
                                  const int begin, const int end) {
  InsertInternal(query_and_result, begin, end, min, max);
}

void QueryCacheRangeIndex::Delete(QueryAndResult *query_and_result) {
  // TODO: assert that it's a between where clause
  const auto where = query_and_result->GetSelectStatement()->whereClause;

  Value lower_bound, upper_bound;

  if (where->opType == hsql::kOpBetween) {
    if (!ExprToValue((*where->exprList)[0], lower_bound)) {
      LOG(ERROR) << "WhereMatch: ExprToValue failed" << std::endl;
    }
    if (!ValueCast(lower_bound, kLL)) {
      LOG(ERROR) << "WhereMatch: ValueCast failed" << std::endl;
    }

    if (!ExprToValue((*where->exprList)[1], upper_bound)) {
      LOG(ERROR) << "WhereMatch: ExprToValue failed" << std::endl;
    }
    if (!ValueCast(upper_bound, kLL)) {
      LOG(ERROR) << "WhereMatch: ValueCast failed" << std::endl;
    }
  } else {
    if (!ExprToValue(where->expr2, lower_bound)) {
      LOG(ERROR) << "WhereMatch: ExprToValue failed" << std::endl;
    }
    if (!ValueCast(lower_bound, kLL)) {
      LOG(ERROR) << "WhereMatch: ValueCast failed" << std::endl;
    }
    upper_bound = lower_bound;
  }

  DeleteInternal(query_and_result, std::get<kLL>(lower_bound),
                 std::get<kLL>(upper_bound), min, max);
}

void QueryCacheRangeIndex::Delete(QueryAndResult *query_and_result,
                                  const int begin, const int end) {
  DeleteInternal(query_and_result, begin, end, min, max);
}

void QueryCacheRangeIndex::InsertInternal(QueryAndResult *query_and_result,
                                          const int begin, const int end,
                                          const int node_begin,
                                          const int node_end) {
  if (begin == node_begin && end == node_end) {
    auto obj = std::make_pair(Range{begin, end}, Node(query_and_result));
    range_index.emplace_or_visit(std::move(obj), [&](auto &node_it) {
      auto &[_, node] = node_it;
      std::unique_lock node_lock(*node.mutex_ptr);
      node.query_and_results.insert(query_and_result);
    });
    return;
  }
  long long mid = ((long long)node_begin + (long long)node_end) / 2;
  if (end <= mid) {
    InsertInternal(query_and_result, begin, end, node_begin, mid);
  } else if (begin > mid) {
    InsertInternal(query_and_result, begin, end, mid + 1, node_end);
  } else {
    InsertInternal(query_and_result, begin, mid, node_begin, mid);
    InsertInternal(query_and_result, mid + 1, end, mid + 1, node_end);
  }
}

void QueryCacheRangeIndex::DeleteInternal(QueryAndResult *query_and_result,
                                          const int begin, const int end,
                                          const int node_begin,
                                          const int node_end) {
  if (begin == node_begin && end == node_end) {
    range_index.erase_if(Range{begin, end}, [&](auto &node_it) {
      auto &[_, node] = node_it;
      std::unique_lock node_lock(*node.mutex_ptr);
      node.query_and_results.erase(query_and_result);
      return node.query_and_results.empty();
    });
    return;
  }
  long long mid = ((long long)node_begin + (long long)node_end) / 2;
  if (end <= mid) {
    DeleteInternal(query_and_result, begin, end, node_begin, mid);
  } else if (begin > mid) {
    DeleteInternal(query_and_result, begin, end, mid + 1, node_end);
  } else {
    DeleteInternal(query_and_result, begin, mid, node_begin, mid);
    DeleteInternal(query_and_result, mid + 1, end, mid + 1, node_end);
  }
}