#include <cassert>
#include <iostream>
#include <random>
#include <string>
#include <unistd.h>

#include "service.hpp"

using Operation = Packet::Operation;

lite::DeserializeResult Feed(Packet &packet, std::string_view text) {
  auto begin = reinterpret_cast<uint8_t *>(const_cast<char *>(text.data()));
  auto end = begin + text.size();
  auto result = packet.Deserialize(begin, end);
  if (result != lite::kBad) assert(begin == end);
  return result;
}

std::string Wire(const Packet &packet) {
  return {packet.buffer->begin(), packet.buffer->end()};
}

void Check(const std::string &wire, Operation operation) {
  for (size_t split = 0; split <= wire.size(); ++split) {
    Packet packet;
    assert(Feed(packet, std::string_view(wire).substr(0, split)) ==
           (split == wire.size() ? lite::kGood : lite::kIndeterminate));
    assert(Feed(packet, std::string_view(wire).substr(split)) == lite::kGood);
    assert(packet.operation == operation);
    assert(packet.Complete(*packet.buffer));
    assert(Wire(packet) == wire);
  }
  Packet packet;
  for (size_t i = 0; i < wire.size(); ++i) {
    assert(Feed(packet, std::string_view(wire).substr(i, 1)) ==
           (i + 1 == wire.size() ? lite::kGood : lite::kIndeterminate));
    if (i + 1 < wire.size())
      assert(Feed(packet, "") == lite::kIndeterminate);
  }
  assert(packet.operation == operation && Wire(packet) == wire);
}

void Parser() {
  const std::string binary("a\r\n\0END\r\nz", 10);
  for (const auto &command : {"set", "add", "replace"}) {
    Check(std::string(command) + " example 17 0 5\r\nhello\r\n",
          Operation::kSet);
    Check(std::string(command) + " example 0 -1 0\r\n\r\n", Operation::kSet);
    Check(std::string(command) + "  example  17  0 10 \r\n" + binary + "\r\n",
          Operation::kSet);
  }
  Check("get one two three\r\n", Operation::kGet);
  Check("get  one  two \r\n", Operation::kGet);
  Check("STORED\r\n", Operation::kSetResp);
  Check("NOT_STORED\r\n", Operation::kSetResp);
  Check("version\r\n", Operation::kVersion);
  Check("VERSION 1.6.14\r\n", Operation::kVersion);
  Check("quit\r\n", Operation::kQuit);
  Check("END\r\n", Operation::kGetRespEnd);
  Check("ERROR\r\n", Operation::kError);
  Check("CLIENT_ERROR bad command line format\r\n", Operation::kError);
  Check("SERVER_ERROR out of memory\r\n", Operation::kError);
  Check("VALUE example 17 10\r\n" + binary +
        "\r\nVALUE other 0 0\r\n\r\nEND\r\n", Operation::kGetRespEnd);
  std::string many;
  for (int i = 0; i < 300; ++i) many += "VALUE key 0 0\r\n\r\n";
  many += "END\r\n";
  Packet packet;
  assert(Feed(packet, many) == lite::kGood);

  const std::vector<std::string> frames = {
      "set key 0 0 0\r\n\r\n", "get key\r\n", "STORED\r\n",
      "VALUE key 0 2\r\n\r\n\r\nEND\r\n", "NOT_STORED\r\n"};
  std::string coalesced;
  for (const auto &frame : frames) coalesced += frame;
  auto begin = reinterpret_cast<uint8_t *>(coalesced.data());
  const auto end = begin + coalesced.size();
  for (const auto &frame : frames) {
    Packet current;
    const auto before = begin;
    assert(current.Deserialize(begin, end) == lite::kGood);
    assert(size_t(begin - before) == frame.size() && Wire(current) == frame);
  }
  assert(begin == end);

  for (const std::string invalid : {
           "\n", "set\r\n", "set key 0 0 -1\r\n", "set key 0 0 +1\r\n",
           "set key 0 0 1x\r\n", "set key 0 0 18446744073709551616\r\n",
           "set key 0 0 18446744073709551615\r\n",
           "set key 4294967296 0 0\r\n", "set key a 0 0\r\n",
           "set key 0 x 0\r\n", "set key 0 0 0 noreply\r\n",
           "set key 0 0 1\r\nx!\n", "set key 0 0 1\r\nx\r!",
           "setx key 0 0 0\r\n", "getx key\r\n", "get\r\n",
           "get bad\tkey\r\n", "get key\rx\n", "get key\n",
           "STORED_EXTRA\r\n", "END extra\r\n", "quit extra\r\n",
           "VALUE key 0 x\r\n", "VALUE key 0 0\r\n\r\nSTORED\r\n",
           "delete key\r\n"}) {
    Packet bad;
    assert(Feed(bad, invalid) == lite::kBad);
    assert(Feed(bad, "get key\r\n") == lite::kBad);
  }
  Packet long_key;
  assert(Feed(long_key, "get " + std::string(251, 'x') + "\r\n") == lite::kBad);
  Check("get " + std::string(250, 'x') + "\r\n", Operation::kGet);
  std::cout << "PASS: all splits, byte feeds, empty feeds, binary values, "
               "300 VALUEs, coalescing, malformed and sticky rejection\n";
}

void Randomized() {
  std::mt19937 random(20261005);
  for (int trial = 0; trial < 1000; ++trial) {
    std::string body(random() % 2048, '\0');
    for (auto &c : body) c = static_cast<char>(random() % 256);
    auto wire = "set key 17 0 " + std::to_string(body.size()) + "\r\n" +
                body + "\r\n";
    if (trial % 2)
      wire = "VALUE key 17 " + std::to_string(body.size()) + "\r\n" +
             body + "\r\nEND\r\n";
    Packet packet;
    size_t pos = 0;
    while (pos < wire.size()) {
      const auto count = std::min(size_t(1 + random() % 79), wire.size() - pos);
      const auto result = Feed(packet, std::string_view(wire).substr(pos, count));
      pos += count;
      assert(result == (pos == wire.size() ? lite::kGood : lite::kIndeterminate));
    }
    assert(Wire(packet) == wire && packet.Complete(*packet.buffer));
  }
  std::cout << "PASS: 1000 seeded binary frames with random fragmentation\n";
}

#ifndef ASCII_EMBEDDED
using Key = std::vector<uint8_t>;
using Inner = lite::CacheInner<Memcached, Packet, Packet, ConnectionInfo, Key,
                              CacheEntry>;
using LoggerInner = lite::LoggerInner<Memcached, Packet, Packet, ConnectionInfo,
                                     Key, CacheEntry>;
using Log = lite::LogEntry<Memcached, Packet, Packet, ConnectionInfo, Key,
                          CacheEntry>;
using Cache = lite::Cache<Memcached, Packet, Packet, ConnectionInfo, Key,
                          CacheEntry>;

std::shared_ptr<Packet> Request(std::string_view text) {
  auto packet = std::make_shared<Packet>();
  assert(Feed(*packet, text) == lite::kGood);
  return packet;
}

void Adapter() {
  std::atomic<bool> emergency{false};
  Inner inner(1024 * 1024, emergency);
  LoggerInner logger(std::chrono::milliseconds(1000));
  Log head(nullptr, nullptr, nullptr);
  Cache cache(inner, logger, &head);
  Memcached app;
  ConnectionInfo info;
  const Key key{'e', 'x', 'a', 'm', 'p', 'l', 'e'};
  auto partial = std::make_shared<Packet>();
  assert(Feed(*partial, "set example 17 0 5\r\n") == lite::kIndeterminate);
  auto stored = Request("STORED\r\n");
  app.NormalUpdate(stored, {partial}, info, &cache);
  CacheEntry entry;
  assert(!cache.Get(key, entry));
  assert(app.EmergencyServe(partial, info, &cache, nullptr, false).second);
  assert(Feed(*partial, "hello\r\n") == lite::kGood);
  app.NormalUpdate(stored, {partial}, info, &cache);
  assert(cache.Get(key, entry));
  assert(std::string(entry.value->begin(), entry.value->end()) == "hello");
  assert(std::string(entry.flags->begin(), entry.flags->end()) == "17");
  auto replay = entry.ToRequest(key);
  assert(Wire(*replay) == "set example 17 0 5\r\nhello\r\n");
  auto replay_parsed = Request(Wire(*replay));
  assert(replay_parsed->StorageComplete(*replay_parsed->buffer));

  for (const auto &reply : {"NOT_STORED\r\n", "ERROR\r\n",
                            "SERVER_ERROR memory\r\n"}) {
    auto failed = Request("set example 0 0 3\r\nbad\r\n");
    app.NormalUpdate(Request(reply), {failed}, info, &cache);
    assert(cache.Get(key, entry));
    assert(std::string(entry.value->begin(), entry.value->end()) == "hello");
  }
  for (const auto &reply : {"", "N", "NO"}) {
    auto short_response = std::make_shared<Packet>();
    short_response->buffer->assign(reply, reply + strlen(reply));
    app.NormalUpdate(short_response, {partial}, info, &cache);
  }
  const std::string binary("a\r\n\0b", 5);
  for (const auto &command : {"add", "replace", "set"}) {
    auto request = Request(std::string(command) + " example 23 0 5\r\n" +
                           binary + "\r\n");
    app.NormalUpdate(stored, {request}, info, &cache);
    assert(cache.Get(key, entry));
    assert(std::string(entry.value->begin(), entry.value->end()) == binary);
    auto [response, close] = app.EmergencyServe(
        Request("get  example missing \r\n"), info, &cache, nullptr, false);
    assert(!close);
    assert(Wire(response) == "VALUE example 23 5\r\n" + binary + "\r\nEND\r\n");
  }
  auto truncated = Request("set example 0 0 3\r\nbad\r\n");
  truncated->buffer->pop_back();
  app.NormalUpdate(stored, {truncated}, info, &cache);
  assert(cache.Get(key, entry));
  assert(std::string(entry.value->begin(), entry.value->end()) == binary);

  lite::ThreadSafeQueue<std::pair<std::shared_ptr<Packet>, bool>> pending;
  pending.push_back({partial, true});
  pending.push_back({replay_parsed, false});
  auto [first, forward_first] = app.Match(stored, info, pending);
  auto [second, forward_second] = app.Match(stored, info, pending);
  assert(first.size() == 1 && first[0] == partial && forward_first);
  assert(second.size() == 1 && second[0] == replay_parsed && !forward_second);
  assert(pending.empty());
  std::cout << "PASS: real cache incomplete/truncated rejection, exact binary "
               "values, add/replace keys, failed replies, replay and matching\n";
}
#else
using Inner = lite::CacheInner<Memcached, Packet, Packet, ConnectionInfo,
                              CacheKey, CacheEntry>;
using Cache = lite::Cache<Memcached, Packet, Packet, ConnectionInfo,
                          CacheKey, CacheEntry>;

ShmSharedPtr<Packet> Request(std::string_view text) {
  auto packet = ShmMakeShared(
      shm->construct<Packet>(bip::anonymous_instance)(), *shm);
  assert(Feed(*packet, text) == lite::kGood);
  return packet;
}

void Adapter() {
  Memcached::DelayedConstructor();
  ShmAtomic<bool> emergency{false};
  auto inner = shm->construct<Inner>(bip::anonymous_instance)(
      1024, &emergency, shm->get_segment_manager());
  Cache cache(inner, nullptr, nullptr);
  ConnectionInfo info;
  CacheKey key(shm->get_segment_manager());
  const std::string name = "example";
  key.assign(name.begin(), name.end());
  auto partial = ShmMakeShared(
      shm->construct<Packet>(bip::anonymous_instance)(), *shm);
  assert(Feed(*partial, "set example 17 0 5\r\n") == lite::kIndeterminate);
  assert(Memcached::EmergencyServe(partial, info, &cache, nullptr, false).second);
  CacheEntry entry;
  assert(!cache.Get(key, entry));
  assert(Feed(*partial, "hello\r\n") == lite::kGood);
  auto [stored, close] =
      Memcached::EmergencyServe(partial, info, &cache, nullptr, false);
  assert(!close && Wire(stored) == "STORED\r\n");
  const std::string binary("a\r\n\0b", 5);
  for (const auto &command : {"add", "replace", "set"}) {
    auto request = Request(std::string(command) + " example 23 0 5\r\n" +
                           binary + "\r\n");
    auto result =
        Memcached::EmergencyServe(request, info, &cache, nullptr, false);
    assert(!result.second && Wire(result.first) == "STORED\r\n");
    assert(cache.Get(key, entry));
    assert(std::string(entry.value.begin(), entry.value.end()) == binary);
    assert(std::string(entry.flags.begin(), entry.flags.end()) == "23");
    const auto replay = entry.ToRequest(key);
    assert(Wire(*replay) == "set example 23 0 5\r\n" + binary + "\r\n");
    auto parsed = Request(Wire(*replay));
    assert(parsed->StorageComplete(*parsed->buffer));
    auto [response, response_close] = Memcached::EmergencyServe(
        Request("get  example missing \r\n"), info, &cache, nullptr, false);
    assert(!response_close);
    assert(Wire(response) == "VALUE example 23 5\r\n" + binary + "\r\nEND\r\n");
  }
  auto truncated = Request("set example 0 0 3\r\nbad\r\n");
  truncated->buffer->pop_back();
  assert(Memcached::EmergencyServe(truncated, info, &cache, nullptr, false).second);
  assert(cache.Get(key, entry));
  assert(std::string(entry.value.begin(), entry.value.end()) == binary);
  entry.flags.push_back(' ');
  assert(Wire(*entry.ToRequest(key)) ==
         "set example 23 0 5\r\n" + binary + "\r\n");

  // The unlinked fixture segment owns the cache until main unmaps it.
  // Cache deletion/destruction is separate from framing qualification.
  std::cout << "PASS: embedded service/shared cache incomplete and truncated "
               "rejection, binary values, add/replace keys, GET and replay\n";
}
#endif

int main(int argc, char **argv) {
  assert(argc == 2);
  google::InitGoogleLogging(argv[0]);
#ifdef ASCII_EMBEDDED
  const auto name = "litelib-ascii-framing-" + std::to_string(getpid());
  SharedMemory memory(bip::create_only, name.c_str(), 16 * 1024 * 1024);
  assert(bip::shared_memory_object::remove(name.c_str()));
  shm = &memory;
#endif
  const std::string test = argv[1];
  if (test == "parser") Parser();
  else if (test == "randomized") Randomized();
  else if (test == "adapter") Adapter();
  else assert(false);
  google::ShutdownGoogleLogging();
}
