#pragma once

#include <event.h>

#include <list>
#include <map>
#include <memory>
#include <set>
#include <string>

#include "packet.hpp"

enum class CacheEntryType { STRING };  // TODO: support more types
// enum class CacheEntryType { STRING, LIST, SET, MAP, ZSET };

using CacheKey = ShmString;

struct CacheEntry {
  CacheEntryType type = CacheEntryType::STRING;
  ShmString value;  // TODO: use offset_ptr and shared_ptr
  // std::shared_ptr<std::list<std::string>> list_value = nullptr;
  // std::shared_ptr<std::set<std::string>> set_value = nullptr;
  // std::shared_ptr<std::map<std::string, std::string>> map_value = nullptr;
  // std::shared_ptr<std::map<double, std::string>> sorted_set_value = nullptr;

  CacheEntry(ShmVoidAllocator allocator) : value(allocator) {}

  size_t GetSize() const {
    switch (type) {
      case CacheEntryType::STRING:
        return value.size();
      // case CacheEntryType::LIST:
      //   return (list_value ? list_value->size() : 0);
      // case CacheEntryType::SET:
      //   return (set_value ? set_value->size() : 0);
      // case CacheEntryType::MAP:
      //   return (map_value ? map_value->size() : 0);
      // case CacheEntryType::ZSET:
      //   return (sorted_set_value ? sorted_set_value->size() : 0);
      default:
        return 0;
    }
  }

  ShmSharedPtr<Packet> ToRequest(const CacheKey &key) const {
    ShmUniquePtrWithDeleter<RESPArray, RESPTypeDeleter> commands(
        shm->get_segment_manager()->template construct<RESPArray>(
            bip::anonymous_instance)(),
        RESPTypeDeleter{shm->get_segment_manager()});

    switch (type) {
      case CacheEntryType::STRING:
        commands->value.emplace_back(
            shm->get_segment_manager()->template construct<RESPBulkString>(
                bip::anonymous_instance)(ShmMakeShared(
                shm->get_segment_manager()->template construct<ShmString>(
                    bip::anonymous_instance)("SET", shm->get_segment_manager()),
                *shm)),
            RESPTypeDeleter{shm->get_segment_manager()});
        commands->value.emplace_back(
            shm->get_segment_manager()->template construct<RESPBulkString>(
                bip::anonymous_instance)(ShmMakeShared(
                shm->get_segment_manager()->template construct<ShmString>(
                    bip::anonymous_instance)(key, shm->get_segment_manager()),
                *shm)),
            RESPTypeDeleter{shm->get_segment_manager()});
        commands->value.emplace_back(
            shm->get_segment_manager()->template construct<RESPBulkString>(
                bip::anonymous_instance)(ShmMakeShared(
                shm->get_segment_manager()->template construct<ShmString>(
                    bip::anonymous_instance)(value.c_str(),
                                             shm->get_segment_manager()),
                *shm)),
            RESPTypeDeleter{shm->get_segment_manager()});
        break;
      // case CacheEntryType::LIST:
      //   commands->value.push_back(std::make_unique<RESPBulkString>(
      //       std::make_shared<std::string>("RPUSH")));
      //   commands->value.push_back(std::make_unique<RESPBulkString>(
      //       std::make_shared<std::string>(key)));
      //   for (const auto &item : *list_value) {
      //     commands->value.push_back(std::make_unique<RESPBulkString>(
      //         std::make_shared<std::string>(item)));
      //   }
      //   break;
      // case CacheEntryType::SET:
      //   commands->value.push_back(std::make_unique<RESPBulkString>(
      //       std::make_shared<std::string>("SADD")));
      //   commands->value.push_back(std::make_unique<RESPBulkString>(
      //       std::make_shared<std::string>(key)));
      //   for (const auto &item : *set_value) {
      //     commands->value.push_back(std::make_unique<RESPBulkString>(
      //         std::make_shared<std::string>(item)));
      //   }
      //   break;
      // case CacheEntryType::MAP:
      //   commands->value.push_back(std::make_unique<RESPBulkString>(
      //       std::make_shared<std::string>("HMSET")));
      //   commands->value.push_back(std::make_unique<RESPBulkString>(
      //       std::make_shared<std::string>(key)));
      //   for (const auto &[field, value] : *map_value) {
      //     commands->value.push_back(std::make_unique<RESPBulkString>(
      //         std::make_shared<std::string>(field)));
      //     commands->value.push_back(std::make_unique<RESPBulkString>(
      //         std::make_shared<std::string>(value)));
      //   }
      //   break;
      // case CacheEntryType::ZSET:
      //   commands->value.push_back(std::make_unique<RESPBulkString>(
      //       std::make_shared<std::string>("ZADD")));
      //   commands->value.push_back(std::make_unique<RESPBulkString>(
      //       std::make_shared<std::string>(key)));
      //   for (const auto &entry : *sorted_set_value) {
      //     const auto &score = entry.first;
      //     const auto &member = entry.second;
      //     commands->value.push_back(std::make_unique<RESPBulkString>(
      //         std::make_shared<std::string>(std::to_string(score))));
      //     commands->value.push_back(std::make_unique<RESPBulkString>(
      //         std::make_shared<std::string>(member)));
      //   }
      //   break;
      default:
        break;
    }

    return ShmMakeShared(shm->get_segment_manager()->template construct<Packet>(
                             bip::anonymous_instance)(boost::move(commands)),
                         *shm);
  }

 private:
  inline void AppendBulkString(std::vector<uint8_t> &buffer,
                               const std::string &str) const {
    buffer.push_back('$');
    const auto str_length = std::to_string(str.size());
    buffer.insert(buffer.end(), str_length.begin(), str_length.end());
    buffer.push_back('\r');
    buffer.push_back('\n');
    buffer.insert(buffer.end(), str.begin(), str.end());
    buffer.push_back('\r');
    buffer.push_back('\n');
  }
};

struct ConnectionInfo {
  bool is_in_transaction_ = false;
  ShmVector<ShmSharedPtr<Packet>> transactions_;

  ConnectionInfo(ShmVoidAllocator allocator) : transactions_(allocator) {}
};

class Redis {
  using Cache =
      lite::Cache<Redis, Packet, Packet, ConnectionInfo, CacheKey, CacheEntry>;
  using Logger =
      lite::Logger<Redis, Packet, Packet, ConnectionInfo, CacheKey, CacheEntry>;

 public:
  void DelayedConstructor();

  std::pair<std::vector<ShmSharedPtr<Packet>>, bool> Match(
      const ShmSharedPtr<Packet> &resp, ConnectionInfo &conn,
      lite::ShmThreadSafeQueue<bip::pair<ShmSharedPtr<Packet>, bool>>
          &pending_requests) const;

  static int EmbeddedNormalUpdate(void *request, ConnectionInfo &conn,
                                  Cache *cache);

  void NormalUpdate(const ShmSharedPtr<Packet> &resp,
                    std::vector<ShmSharedPtr<Packet>> requests,
                    ConnectionInfo &conn, Cache *cache);

  void HandleReplayResponse(const ShmSharedPtr<Packet> &resp,
                            std::vector<ShmSharedPtr<Packet>> requests,
                            ConnectionInfo &conn, Cache *cache);

  std::pair<Packet, bool> EmergencyServe(ShmSharedPtr<Packet> req,
                                         ConnectionInfo &conn, Cache *cache,
                                         Logger *logger, bool flow_control);

  void NormalToEmergencyHook() {}

  void EmergencyToNormalHook() {}

  std::optional<Packet> EmergencyConnectionEstablishHook(ConnectionInfo &conn) {
    return std::nullopt;
  }

 private:
  void NormalUpdateImpl(const ShmSharedPtr<Packet> &req, Cache *cache,
                        const bool in_transaction = false);

  std::pair<RESPType *, bool> EmergencyServeImpl(
      ShmSharedPtr<Packet> req, ConnectionInfo &conn, Cache *cache,
      Logger *logger, bool flow_control, const bool in_transaction = false);

  std::pair<RESPType *, bool> HandleUpdate(ShmSharedPtr<Packet> req,
                                           ConnectionInfo &conn, Cache *cache,
                                           Logger *logger, bool flow_control,
                                           const bool in_transaction = false,
                                           const bool in_emergency = false);

  static ShmSharedPtr<Packet> abort_req_;
};