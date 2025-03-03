#pragma once

#include <event.h>

#include <list>
#include <lite.hpp>
#include <map>
#include <memory>
#include <set>
#include <string>

#include "embedded_full.hpp"

class RESPPacket;

enum class CacheEntryType { STRING, MAP };

extern SharedMemory *shm;

using MapType = ShmMap<ShmString, ShmString>;

using CacheKey = ShmString;

struct CacheEntry {
  CacheEntryType type = CacheEntryType::STRING;
  ShmString value;
  ShmSharedPtr<MapType> map_value;

  CacheEntry(ShmVoidAllocator allocator) : value(allocator) {}

  size_t GetSize() const {
    switch (type) {
      case CacheEntryType::STRING:
        return value.size();
      case CacheEntryType::MAP:
        return (map_value ? map_value->size() : 0);
      default:
        return 0;
    }
  }

  void SetType(CacheEntryType type) {
    type = type;
    value.clear();
    map_value.reset();
  }

  ShmSharedPtr<RESPPacket> ToRequest(const CacheKey &key) const;
};

struct ConnectionInfo {
  bool is_in_transaction_ = false;
  ShmVector<ShmSharedPtr<RESPPacket>> transactions;

  ConnectionInfo(ShmVoidAllocator allocator) : transactions(allocator) {}

  void Reset() {
    is_in_transaction_ = false;
    transactions.clear();
  }
};

class Redis {
  using Cache = lite::Cache<Redis, RESPPacket, RESPPacket, ConnectionInfo,
                            CacheKey, CacheEntry>;
  using Logger = lite::Logger<Redis, RESPPacket, RESPPacket, ConnectionInfo,
                              CacheKey, CacheEntry>;

 public:
  static void DelayedConstructor();

  static int EmbeddedNormalUpdate(void *request, ConnectionInfo &conn,
                                  Cache *cache,
                                  RequestDestructorFn RequestDestructor);

  static std::pair<RESPPacket, bool> EmergencyServe(
      ShmSharedPtr<RESPPacket> req, ConnectionInfo &conn, Cache *cache,
      Logger *logger, bool flow_control);

  static void NormalToEmergencyHook() {}

  static void EmergencyToNormalHook() {}

  static std::optional<RESPPacket> EmergencyConnectionEstablishHook(
      ConnectionInfo &conn);

  static RequestDestructorFn RequestDestructor;

 private:
  static std::optional<std::pair<RESPPacket, bool>> HandleRequestForConnection(
      ShmSharedPtr<RESPPacket> req, ConnectionInfo &conn, Cache *cache,
      Logger *logger = nullptr, bool flow_control = false);

  static std::optional<std::pair<RESPPacket, bool>> HandleSingleRequest(
      EmbeddedRequest *req, Cache *cache, const bool in_transaction = false,
      Logger *logger = nullptr, bool flow_control = false);

  static ShmSharedPtr<RESPPacket> abort_req_;
};