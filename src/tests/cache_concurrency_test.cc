#include <atomic>
#include <barrier>
#include <cassert>
#include <chrono>
#include <iostream>
#include <memory>
#include <set>
#include <string>
#include <thread>
#include <vector>

#include <glog/logging.h>

#include "cache.hpp"
#include "cache_inner_impl.hpp"
#include "cache_impl.hpp"
#include "logger_inner_impl.hpp"

struct Message {
  std::shared_ptr<std::vector<uint8_t>> Serialize() { return nullptr; }
  lite::DeserializeResult Deserialize(uint8_t *&, uint8_t *) {
    return lite::kGood;
  }
};

struct Entry {
  std::shared_ptr<std::string> value;
  size_t GetSize() const { return value->size(); }
};

struct Application {};
struct ConnectionInfo {};
using Inner = lite::CacheInner<Application, Message, Message, ConnectionInfo, int, Entry>;
using Logger = lite::LoggerInner<Application, Message, Message, ConnectionInfo, int, Entry>;
using Log = lite::LogEntry<Application, Message, Message, ConnectionInfo, int, Entry>;
using Cache = lite::Cache<Application, Message, Message, ConnectionInfo, int, Entry>;

Entry entry(int value) {
  return {std::make_shared<std::string>(64, static_cast<char>('a' + value % 26))};
}

void serial_cases(bool emergency_mode) {
  std::atomic<bool> emergency{emergency_mode};
  Inner inner(1024, emergency);
  Logger logger(std::chrono::milliseconds(1000));
  Log head(nullptr, nullptr, nullptr);
  Cache cache(inner, logger, &head);
  assert(cache.Add(1, entry(1)));
  assert(!cache.Add(1, entry(2)));
  assert(cache.Replace(1, entry(3)));
  Entry value;
  assert(cache.Get(1, value));
  assert(*value.value == *entry(3).value);
  Log *log = nullptr;
  if (emergency_mode) {
    assert(logger.Pop(log));
    assert(log->state->key == 1 && *log->state->value.value == *value.value);
    delete log;
  }
  assert(logger.Empty());
  assert(cache.Replace(1, entry(4), false, false));
  assert(logger.Empty());
  assert(cache.Delete(1));
  assert(!cache.Get(1, value));
  assert(!cache.Delete(1));
  assert(!cache.Replace(1, entry(5)));
  assert(logger.Empty());
  {
    auto transaction = cache.TransactionLock();
    assert(cache.Add(2, entry(6), true));
    assert(cache.Replace(2, entry(7), true));
    assert(cache.Get(2, value, true));
    assert(*value.value == *entry(7).value);
    assert(cache.Delete(2, true));
  }
  assert(logger.Empty());
  assert(cache.Add(3, entry(8)));
  assert(cache.Replace(3, entry(9), false, false));
  assert(logger.Empty());
  assert(cache.Delete(3));
  emergency = false;
  assert(cache.Add(4, entry(10)));
  assert(logger.Empty());
  assert(cache.Delete(4));
  assert(head.conn_nxt == nullptr);
  std::cout << "PASS: serial add/replace/get/delete, replay, log=false and transaction; emergency="
            << emergency_mode << "\n";
}

void concurrent_delete(bool emergency_mode) {
  std::atomic<bool> emergency{emergency_mode};
  Inner inner(1024, emergency);
  Logger logger(std::chrono::milliseconds(1000));
  Log head(nullptr, nullptr, nullptr);
  Cache cache(inner, logger, &head);
  std::barrier phase(4);
  std::atomic<int> deleted{0};
  std::vector<std::thread> threads;
  for (int worker = 0; worker < 4; ++worker) {
    threads.emplace_back([&, worker] {
      for (int iteration = 0; iteration < 1000; ++iteration) {
        if (worker == 0) {
          deleted = 0;
          assert(cache.Add(1, entry(iteration)));
        }
        phase.arrive_and_wait();
        if (cache.Delete(1)) ++deleted;
        phase.arrive_and_wait();
        if (worker == 0) {
          assert(deleted == 1);
          assert(logger.Empty());
          assert(head.conn_nxt == nullptr);
        }
        phase.arrive_and_wait();
      }
    });
  }
  for (auto &thread : threads) thread.join();
  std::cout << "PASS: concurrent delete is atomic; emergency=" << emergency_mode << "\n";
}

void normal_eviction() {
  std::atomic<bool> emergency{false};
  Inner inner(128, emergency);
  Logger logger(std::chrono::milliseconds(1000));
  Log head(nullptr, nullptr, nullptr);
  Cache cache(inner, logger, &head);
  assert(cache.Add(1, entry(1)));
  assert(cache.Add(2, entry(2)));
  assert(cache.Add(3, entry(3)));
  Entry value;
  assert(!cache.Get(1, value));
  assert(cache.Get(2, value));
  assert(cache.Get(3, value));
  assert(logger.Empty());
  std::cout << "PASS: normal byte-capacity eviction preserved\n";
}

void run(int workers, int iterations, bool emergency_mode, int keys) {
  std::atomic<bool> emergency{emergency_mode};
  Inner inner(1024 * 1024, emergency);
  Logger logger(std::chrono::milliseconds(1000));
  std::vector<std::unique_ptr<Log>> heads;
  for (int i = 0; i < workers; ++i) {
    heads.push_back(std::make_unique<Log>(nullptr, nullptr, nullptr));
  }
  std::barrier start(workers);
  std::vector<std::thread> threads;
  for (int worker = 0; worker < workers; ++worker) {
    threads.emplace_back([&, worker] {
      Cache cache(inner, logger, heads[worker].get());
      start.arrive_and_wait();
      for (int i = 0; i < iterations; ++i) {
        const int key = (i + worker) % keys;
        assert(cache.Set(key, entry(i + worker)));
        Entry value;
        assert(cache.Get(key, value));
        assert(value.value && value.value->size() == 64);
      }
    });
  }
  for (auto &thread : threads) thread.join();
  Log *log = nullptr;
  int dirty_entries = 0;
  std::set<int> replayed;
  while (logger.Pop(log)) {
    assert(log->state && log->state->key >= 0 && log->state->key < keys);
    assert(replayed.insert(log->state->key).second);
    Cache cache(inner, logger, heads.front().get());
    Entry final_value;
    assert(cache.Get(log->state->key, final_value));
    assert(*log->state->value.value == *final_value.value);
    delete log;
    ++dirty_entries;
  }
  assert(dirty_entries == (emergency_mode ? keys : 0));
  for (const auto &head : heads) assert(head->conn_nxt == nullptr);
  std::cout << "PASS: " << workers << " writers, " << iterations
            << " iterations, " << keys << " keys; " << dirty_entries
            << " unique replay entries retained; emergency=" << emergency_mode << "\n";
}

int main(int argc, char **argv) {
  google::InitGoogleLogging(argv[0]);
  if (argc > 1 && std::string(argv[1]) == "serial") {
    serial_cases(false);
    serial_cases(true);
    normal_eviction();
    return 0;
  }
  if (argc > 1 && std::string(argv[1]) == "delete") {
    concurrent_delete(false);
    concurrent_delete(true);
    return 0;
  }
  const int workers = argc > 1 ? std::stoi(argv[1]) : 8;
  const int iterations = argc > 2 ? std::stoi(argv[2]) : 10000;
  const bool emergency = argc > 3 ? std::stoi(argv[3]) != 0 : true;
  const int keys = argc > 4 ? std::stoi(argv[4]) : 1;
  assert(workers >= 1 && workers <= 16 && iterations > 0);
  assert(keys > 0 && keys <= iterations);
  run(workers, iterations, emergency, keys);
}
