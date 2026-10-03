#include <atomic>
#include <barrier>
#include <cassert>
#include <chrono>
#include <iostream>
#include <set>
#include <string>
#include <thread>
#include <unistd.h>
#include <vector>

#include <glog/logging.h>

#include "bip.hpp"
#include "cache.hpp"
#include "cache_inner_impl.hpp"
#include "cache_impl.hpp"
#include "logger_inner_impl.hpp"

struct Message {
  ShmSharedPtr<ShmVector<uint8_t>> Serialize() { return {}; }
  lite::DeserializeResult Deserialize(uint8_t *&, uint8_t *) {
    return lite::kGood;
  }
};

using Fields = ShmMap<ShmString, ShmString>;

struct Entry {
  ShmString scalar;
  ShmSharedPtr<Fields> fields;
  explicit Entry(ShmVoidAllocator allocator) : scalar(allocator) {}
  size_t GetSize() const { return fields ? fields->size() : scalar.size(); }
};

struct Application {};
struct ConnectionInfo {};
using Inner = lite::CacheInner<Application, Message, Message, ConnectionInfo,
                              ShmString, Entry>;
using State = lite::CacheState<Application, Message, Message, ConnectionInfo,
                              ShmString, Entry>;
using Log = lite::LogEntry<Application, Message, Message, ConnectionInfo,
                          ShmString, Entry>;
using Logger = lite::LoggerInner<Application, Message, Message, ConnectionInfo,
                                ShmString, Entry>;
using Cache = lite::Cache<Application, Message, Message, ConnectionInfo,
                          ShmString, Entry>;

int main(int argc, char **argv) {
  assert(argc == 2);
  const std::string test = argv[1];
  const auto unique = std::chrono::steady_clock::now().time_since_epoch().count();
  const auto name = "litelib-embedded-cache-test-" + std::to_string(getpid()) +
                    "-" + std::to_string(unique);
  SharedMemory memory(bip::create_only, name.c_str(), 512 * 1024 * 1024);
  SharedMemory alternate(bip::open_only, name.c_str());
  assert(bip::shared_memory_object::remove(name.c_str()));
  auto allocator = memory.get_segment_manager();
  auto emergency = memory.construct<ShmAtomic<bool>>("emergency")(false);
  const int capacity = test == "load" ? 65536 : 64;
  auto cache = memory.construct<Inner>("cache")(capacity, emergency, allocator);
  if (test == "emergency" || test == "emergency-many") {
    emergency->store(true);
    auto logger = memory.construct<Logger>("logger")(
        std::chrono::milliseconds(1000), allocator);
    std::barrier start(4);
    const int keys = test == "emergency-many" ? 64 : 1;
    std::vector<Log *> heads(4);
    std::vector<std::thread> workers;
    for (int worker = 0; worker < 4; ++worker) {
      workers.emplace_back([&, worker] {
        auto head = memory.construct<Log>(bip::anonymous_instance)(
            nullptr, ShmSharedPtr<Message>{}, allocator);
        heads[worker] = head;
        Cache wrapper(cache, logger, head);
        Entry value(allocator);
        value.scalar = std::string(100, static_cast<char>('a' + worker)).c_str();
        start.arrive_and_wait();
        for (int i = 0; i < 20000; ++i) {
          const auto key_text = "key-" + std::to_string(i % keys);
          ShmString key(key_text.c_str(), allocator);
          assert(wrapper.Set(key, value));
        }
      });
    }
    for (auto &worker : workers) worker.join();
    std::set<std::string> replayed;
    bip::offset_ptr<Log> entry;
    while (logger->Pop(entry)) {
      assert(entry->state);
      assert(replayed.insert(std::string(entry->state->key)).second);
      Entry observed(allocator);
      assert(cache->Get(entry->state->key, observed, false));
      assert(observed.scalar == entry->state->value.scalar);
      entry->~Log();
      cache->log_entry_allocator_.deallocate_one(entry);
    }
    assert(replayed.size() == static_cast<size_t>(keys));
    for (auto head : heads) {
      assert(!head->conn_nxt);
      memory.destroy_ptr(head);
    }
    memory.destroy_ptr(cache);
    memory.destroy_ptr(logger);
    assert(memory.check_sanity());
    std::cout << "PASS: emergency publication, 80000 concurrent writes\n";
    return 0;
  }
  if (test == "api-normal" || test == "api-emergency") {
    emergency->store(test == "api-emergency");
    auto logger = memory.construct<Logger>("logger")(
        std::chrono::milliseconds(1000), allocator);
    auto head = memory.construct<Log>("head")(
        nullptr, ShmSharedPtr<Message>{}, allocator);
    Cache wrapper(cache, logger, head);
    Entry value(allocator);
    value.scalar = "initial";
    ShmString key("key", allocator);
    for (int i = 0; i < 1000; ++i) {
      assert(wrapper.Add(key, value));
      assert(!wrapper.Add(key, value));
      assert(wrapper.Replace(key, value));
      assert(wrapper.Set(key, value));
      assert(wrapper.Replace(key, value, false, false));
      assert(logger->Empty());
      assert(wrapper.Delete(key));
      assert(!wrapper.Delete(key));
      assert(!wrapper.Replace(key, value));
      {
        auto lock = wrapper.TransactionLock();
        assert(wrapper.Add(key, value, true));
        assert(wrapper.Replace(key, value, true));
        assert(wrapper.Set(key, value, true));
        assert(wrapper.Get(key, value, true));
        assert(wrapper.Delete(key, true));
      }
      assert(logger->Empty() && !head->conn_nxt);
    }
    memory.destroy_ptr(head);
    memory.destroy_ptr(logger);
    memory.destroy_ptr(cache);
    assert(memory.check_sanity());
    std::cout << "PASS: " << test << " Add/Replace/Set/Delete, no-log and transactions\n";
    return 0;
  }
  const int iterations =
      test == "evict" || test == "load" || test == "remap" ? 100000 : 3;
  for (int i = 0; i < iterations; ++i) {
    if (test == "remap" && i == iterations / 2) {
      memory.swap(alternate);
      alternate = SharedMemory{};
      allocator = memory.get_segment_manager();
      cache = memory.find<Inner>("cache").first;
      assert(cache);
    }
    const auto text = test == "load" ?
        "user-100000000000000000000" + std::to_string(i) :
        "key-" + std::to_string(i % 128);
    ShmString key(text.c_str(), allocator);
    Entry value(allocator);
    if (!cache->Get(key, value, false)) {
      value.fields = ShmMakeShared(
          memory.construct<Fields>(bip::anonymous_instance)(allocator), memory);
    }
    for (int field = 0; field < 10; ++field) {
      const auto field_text = "field-" + std::to_string(field);
      const auto payload = std::string(100, static_cast<char>('a' + i % 26));
      value.fields->insert_or_assign(ShmString(field_text.c_str(), allocator),
                                     ShmString(payload.c_str(), allocator));
    }
    bip::offset_ptr<State> state;
    assert(cache->Set(key, value, false, nullptr, nullptr, state));
    Entry observed(allocator);
    assert(cache->Get(key, observed, false));
    assert(observed.fields && observed.fields->size() == 10);
  }
  if (test == "delete") {
    ShmString key("key-1", allocator);
    bip::offset_ptr<Log> dirty;
    assert(cache->Delete(key, false, dirty));
    Entry value(allocator);
    assert(!cache->Get(key, value, false));
  } else if (test == "destroy") {
    memory.destroy_ptr(cache);
    cache = nullptr;
  } else {
    assert(test == "evict" || test == "load" || test == "remap");
  }
  if (cache) memory.destroy_ptr(cache);
  assert(memory.check_sanity());
  std::cout << "PASS: " << test << ", " << iterations
            << " ordinary single-worker hash updates\n";
}
