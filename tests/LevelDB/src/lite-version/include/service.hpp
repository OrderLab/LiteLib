#pragma once

#include <event.h>

#include <core.hpp>
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

struct LogEntry {
  std::shared_ptr<Packet> value;

  std::shared_ptr<std::vector<uint8_t>> Serialize() const {
    return value->Serialize();
  }

  lite::DeserializeResult Deserialize(uint8_t *&begin, uint8_t *end) {
    return value->Deserialize(begin, end);
  }

  std::shared_ptr<std::vector<uint8_t>> ToRequests() const {
    return value->Serialize();
  }
};

struct ConnectionInfo {
  bool is_in_transaction_ = false;
  std::vector<std::shared_ptr<Packet>> transactions_;
  std::shared_ptr<bool> log_valid_;
  ConnectionInfo() : log_valid_(std::make_shared<bool>(true)) {}
};

class LevelDB {
  using Cache = lite::Cache<std::string, CacheEntry>;

 public:
  std::optional<std::vector<std::shared_ptr<Packet>>> Filter(
      const std::shared_ptr<Packet> &resp, ConnectionInfo &conn,
      std::deque<std::shared_ptr<Packet>> &pending_requests) const;

  void NormalUpdate(const std::shared_ptr<Packet> &resp,
                    std::vector<std::shared_ptr<Packet>> requests,
                    ConnectionInfo &conn, Cache &cache);

  Packet EmergencyServe(std::shared_ptr<Packet> req, ConnectionInfo &conn,
                        Cache &cache, std::function<void(LogEntry)> log_func,
                        std::function<bool(size_t)> undo_log_func);

 private:
  void NormalUpdateImpl(const std::shared_ptr<Packet> &req, Cache &cache,
                        const bool in_transaction = false);

  RESPType *EmergencyServeImpl(std::shared_ptr<Packet> req,
                               ConnectionInfo &conn, Cache &cache,
                               std::function<void(LogEntry)> log_func,
                               const bool in_transaction = false);
};
