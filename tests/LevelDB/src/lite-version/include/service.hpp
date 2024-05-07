#pragma once

#include <event.h>

#include <memory>
#include <string>

#include "packet.hpp"

struct CacheEntry {
  std::shared_ptr<std::string> value = nullptr;
  size_t GetSize() const { return (value ? value->size() : 0); }

  std::shared_ptr<std::vector<uint8_t>> ToRequests(
      const std::string &key) const {
    std::vector<uint8_t> buffer = {'*',  '3', '\r', '\n', '$',  '3', '\r',
                                   '\n', 'S', 'E',  'T',  '\r', '\n'};
    AppendBulkString(buffer, key);
    AppendBulkString(buffer, *value);
    return std::make_shared<std::vector<uint8_t>>(std::move(buffer));
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
  using Cache = lite::Cache<std::string, CacheEntry, Packet>;
  using Logger = lite::Logger<Packet, std::string, CacheEntry>;

 public:
  std::pair<std::vector<std::shared_ptr<Packet>>, bool> Match(
      const std::shared_ptr<Packet> &resp, ConnectionInfo &conn,
      std::deque<std::pair<std::shared_ptr<Packet>, bool>> &pending_requests)
      const;

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
};
