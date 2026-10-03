#include <barrier>
#include <cassert>
#include <chrono>
#include <iostream>
#include <thread>
#include <unistd.h>

#include "packet.hpp"
#include "service.hpp"

using Inner = lite::CacheInner<Redis, RESPPacket, RESPPacket, ConnectionInfo,
                              CacheKey, CacheEntry>;
using Log = lite::LogEntry<Redis, RESPPacket, RESPPacket, ConnectionInfo,
                          CacheKey, CacheEntry>;
using LoggerInner = lite::LoggerInner<Redis, RESPPacket, RESPPacket,
                                      ConnectionInfo, CacheKey, CacheEntry>;
using Cache = lite::Cache<Redis, RESPPacket, RESPPacket, ConnectionInfo,
                          CacheKey, CacheEntry>;
using Logger = lite::Logger<Redis, RESPPacket, RESPPacket, ConnectionInfo,
                            CacheKey, CacheEntry>;

ShmSharedPtr<RESPPacket> request(std::initializer_list<std::string> arguments) {
  auto packet = ShmMakeShared(
      shm->construct<RESPPacket>(bip::anonymous_instance)(
          shm->get_segment_manager()), *shm);
  packet->argc = arguments.size();
  for (const auto &argument : arguments) {
    packet->argv.emplace_back(argument.c_str(), shm->get_segment_manager());
  }
  return packet;
}

int main() {
  const auto name = "litelib-redis-hash-test-" + std::to_string(getpid()) +
      "-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
  SharedMemory memory(bip::create_only, name.c_str(), 64 * 1024 * 1024);
  assert(bip::shared_memory_object::remove(name.c_str()));
  shm = &memory;
  auto allocator = memory.get_segment_manager();
  auto emergency = memory.construct<ShmAtomic<bool>>("mode")(true);
  auto inner = memory.construct<Inner>("cache")(64, emergency, allocator);
  auto logger = memory.construct<LoggerInner>("logger")(
      std::chrono::milliseconds(1000), allocator);
  std::barrier start(4);
  std::vector<Log *> heads(4);
  std::vector<std::thread> workers;
  for (int worker = 0; worker < 4; ++worker) {
    workers.emplace_back([&, worker] {
      auto head = memory.construct<Log>(bip::anonymous_instance)(
          nullptr, ShmSharedPtr<RESPPacket>{}, allocator);
      heads[worker] = head;
      Cache cache(inner, logger, head);
      Logger wrapper(logger, head);
      ConnectionInfo connection(allocator);
      start.arrive_and_wait();
      for (int i = 0; i < 1000; ++i) {
        const auto field = std::to_string(worker) + ":" + std::to_string(i);
        auto [response, shutdown] = Redis::EmergencyServe(
            request({"HSET", "same-key", field, std::string(100, 'x')}),
            connection, &cache, &wrapper, false);
        assert(!shutdown && response.buffer);
        assert(std::string(response.buffer->begin(), response.buffer->end()) == "+OK\r\n");
        if (i % 20 == 0) {
          auto [read, closed] = Redis::EmergencyServe(
              request({"HGETALL", "same-key"}), connection,
              &cache, &wrapper, false);
          assert(!closed && read.buffer && read.buffer->front() == '*');
        }
      }
    });
  }
  for (auto &worker : workers) worker.join();
  CacheEntry value(allocator);
  assert(inner->Get(ShmString("same-key", allocator), value, false));
  assert(value.map_value && value.map_value->size() == 4000);
  for (int worker = 0; worker < 4; ++worker) {
    for (int i = 0; i < 1000; ++i) {
      auto field = std::to_string(worker) + ":" + std::to_string(i);
      assert(value.map_value->at(ShmString(field.c_str(), allocator)) ==
             ShmString(std::string(100, 'x').c_str(), allocator));
    }
  }
  bip::offset_ptr<Log> entry;
  assert(logger->Pop(entry) && entry->state);
  assert(entry->state->value.map_value->size() == 4000);
  entry->~Log();
  inner->log_entry_allocator_.deallocate_one(entry);
  assert(!logger->Pop(entry));
  {
    Cache cache(inner, logger, heads[0]);
    Logger wrapper(logger, heads[0]);
    ConnectionInfo connection(allocator);
    connection.is_in_transaction_ = true;
    wrapper.Log(request({"MULTI"}));
    for (const auto &command : {
             request({"HSET", "same-key", "transaction-field", "value"}),
             request({"HGETALL", "same-key"}),
             request({"EXEC"})}) {
      auto [response, closed] = Redis::EmergencyServe(
          command, connection, &cache, &wrapper, false);
      assert(!closed && response.buffer);
    }
    assert(value.map_value->size() == 4001);
  }
  while (logger->Pop(entry)) {
    entry->~Log();
    inner->log_entry_allocator_.deallocate_one(entry);
  }
  value.map_value.reset();
  for (auto head : heads) {
    assert(!head->conn_nxt);
    memory.destroy_ptr(head);
  }
  memory.destroy_ptr(inner);
  memory.destroy_ptr(logger);
  assert(memory.check_sanity());
  std::cout << "PASS: 4000 concurrent HSET fields, concurrent HGETALL, one replay entry and existing-transaction EXEC\n";
}
