#pragma once

#include <event.h>

#include <list>
#include <map>
#include <memory>
#include <set>
#include <string>

#include "packet.hpp"

using SharedString =
    bip::basic_string<char, std::char_traits<char>, SharedAllocator<char>>;

enum class CacheEntryType { STRING };  // TODO: support more types
// enum class CacheEntryType { STRING, LIST, SET, MAP, ZSET };

using CacheKey = SharedString;

struct CacheEntry {
  CacheEntryType type = CacheEntryType::STRING;
  SharedString value;  // TODO: use offset_ptr and shared_ptr
  // std::shared_ptr<std::list<std::string>> list_value = nullptr;
  // std::shared_ptr<std::set<std::string>> set_value = nullptr;
  // std::shared_ptr<std::map<std::string, std::string>> map_value = nullptr;
  // std::shared_ptr<std::map<double, std::string>> sorted_set_value = nullptr;

  CacheEntry(SegmentManager *segment_mgr) : value(segment_mgr) {}

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

  std::shared_ptr<Packet> ToRequest(const std::string &key) const {
    auto commands = std::make_unique<RESPArray>();

    switch (type) {
      case CacheEntryType::STRING:
        commands->value.push_back(std::make_unique<RESPBulkString>(
            std::make_shared<std::string>("SET")));
        commands->value.push_back(std::make_unique<RESPBulkString>(
            std::make_shared<std::string>(key)));
        commands->value.push_back(std::make_unique<RESPBulkString>(
            std::make_shared<std::string>(value.c_str())));
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

    return std::make_shared<Packet>(std::move(commands));
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
  std::vector<std::shared_ptr<Packet>> transactions_;
};

class Redis {
  using Cache =
      lite::Cache<Redis, Packet, Packet, ConnectionInfo, CacheKey, CacheEntry>;
  using Logger =
      lite::Logger<Redis, Packet, Packet, ConnectionInfo, CacheKey, CacheEntry>;

 public:
  Redis();

  std::pair<std::vector<std::shared_ptr<Packet>>, bool> Match(
      const std::shared_ptr<Packet> &resp, ConnectionInfo &conn,
      lite::ThreadSafeQueue<std::pair<std::shared_ptr<Packet>, bool>>
          &pending_requests) const;

  void NormalUpdate(const std::shared_ptr<Packet> &resp,
                    std::vector<std::shared_ptr<Packet>> requests,
                    ConnectionInfo &conn, Cache *cache);

  void HandleReplayResponse(const std::shared_ptr<Packet> &resp,
                            std::vector<std::shared_ptr<Packet>> requests,
                            ConnectionInfo &conn, Cache *cache);

  std::pair<Packet, bool> EmergencyServe(std::shared_ptr<Packet> req,
                                         ConnectionInfo &conn, Cache *cache,
                                         Logger *logger, bool flow_control);

  void NormalToEmergencyHook() {}

  void EmergencyToNormalHook() {}

  std::optional<Packet> EmergencyConnectionEstablishHook(ConnectionInfo &conn) {
    return std::nullopt;
  }

 private:
  void NormalUpdateImpl(const std::shared_ptr<Packet> &req, Cache *cache,
                        const bool in_transaction = false);

  std::pair<RESPType *, bool> EmergencyServeImpl(
      std::shared_ptr<Packet> req, ConnectionInfo &conn, Cache *cache,
      Logger *logger, bool flow_control, const bool in_transaction = false);

  std::pair<RESPType *, bool> HandleUpdate(std::shared_ptr<Packet> req,
                                           ConnectionInfo &conn, Cache *cache,
                                           Logger *logger, bool flow_control,
                                           const bool in_transaction = false,
                                           const bool in_emergency = false);

  static std::shared_ptr<Packet> abort_req_;
};