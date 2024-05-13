#pragma once

#include <event.h>

#include <memory>
#include <string>

#include "packet.hpp"

struct CacheEntry {
  std::shared_ptr<std::string> value = nullptr;
  size_t GetSize() const { return (value ? value->size() : 0); }

  std::shared_ptr<Packet> ToRequest(const std::string &key) const {
    auto commands = std::make_unique<RESPArray>();
    commands->value.push_back(
        std::make_unique<RESPBulkString>(std::make_shared<std::string>("SET")));
    commands->value.push_back(
        std::make_unique<RESPBulkString>(std::make_shared<std::string>(key)));
    commands->value.push_back(std::make_unique<RESPBulkString>(value));
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

class LevelDB {
  using Cache = lite::Cache<LevelDB, Packet, Packet, ConnectionInfo,
                            std::string, CacheEntry>;
  using Logger = lite::Logger<LevelDB, Packet, Packet, ConnectionInfo,
                              std::string, CacheEntry>;

 public:
  LevelDB();

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

  Packet EmergencyServe(std::shared_ptr<Packet> req, ConnectionInfo &conn,
                        Cache *cache, Logger *logger);

 private:
  void NormalUpdateImpl(const std::shared_ptr<Packet> &req, Cache *cache,
                        const bool in_transaction = false);

  RESPType *EmergencyServeImpl(std::shared_ptr<Packet> req,
                               ConnectionInfo &conn, Cache *cache,
                               Logger *logger,
                               const bool in_transaction = false);

  static std::shared_ptr<Packet> abort_req_;
};
