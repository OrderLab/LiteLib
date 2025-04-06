#pragma once

#include <event.h>

#include <lite.hpp>
#include <memory>
#include <string>

#include "packet.hpp"

using CacheKey = ShmVector<uint8_t>;

struct CacheEntry {
  ShmVector<uint8_t> value;
  ShmVector<uint8_t> flags;

  CacheEntry(ShmVoidAllocator allocator = shm->get_segment_manager())
      : value(ShmVector<uint8_t>(allocator)),
        flags(ShmVector<uint8_t>(allocator)) {}

  size_t GetSize() const { return value.size() + flags.size(); }

  ShmSharedPtr<Packet> ToRequest(const CacheKey &key) const {
    auto req =
        ShmMakeShared(shm->get_segment_manager()->template construct<Packet>(
                          bip::anonymous_instance)(),
                      *shm);
    const auto size_str = std::to_string(value.size());
    req->buffer->reserve(key.size() + flags.size() + value.size() +
                         size_str.size() + 11);
    req->buffer->push_back('s');
    req->buffer->push_back('e');
    req->buffer->push_back('t');
    req->buffer->push_back(' ');
    req->buffer->insert(req->buffer->end(), key.begin(), key.end());
    req->buffer->push_back(' ');
    req->buffer->insert(req->buffer->end(), flags.begin(), flags.end());
    req->buffer->push_back('0');
    req->buffer->push_back(' ');
    req->buffer->insert(req->buffer->end(), size_str.begin(), size_str.end());
    req->buffer->push_back('\r');
    req->buffer->push_back('\n');
    req->buffer->insert(req->buffer->end(), value.begin(), value.end());
    req->buffer->push_back('\r');
    req->buffer->push_back('\n');
    return req;
  }
};

struct ConnectionInfo {
  ConnectionInfo(ShmVoidAllocator allocator = shm->get_segment_manager()) {}
};

class Memcached {
  using Cache = lite::Cache<Memcached, Packet, Packet, ConnectionInfo, CacheKey,
                            CacheEntry>;
  using Logger = lite::Logger<Memcached, Packet, Packet, ConnectionInfo,
                              CacheKey, CacheEntry>;

 public:
  static void DelayedConstructor();

  static int EmbeddedNormalUpdate(void *request, ConnectionInfo &conn,
                                  Cache *cache,
                                  RequestDestructorFn RequestDestructor);

  static std::pair<Packet, bool> EmergencyServe(ShmSharedPtr<Packet> p,
                                                ConnectionInfo &conn_info,
                                                Cache *cache, Logger *logger,
                                                bool flow_control);

  static std::pair<Packet, bool> EmergencyServeImpl(ShmSharedPtr<Packet> p,
                                                    Cache *cache,
                                                    Logger *logger,
                                                    bool flow_control);

  static void NormalToEmergencyHook() {}

  static void EmergencyToNormalHook() {}

  static std::optional<Packet> EmergencyConnectionEstablishHook(
      ConnectionInfo &conn_info) {
    return std::nullopt;
  }

  static RequestDestructorFn RequestDestructor;

 private:
  static Packet *stored, *not_stored, *null_resp, *version;
};
