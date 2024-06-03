#pragma once

#include <event.h>

#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

#include "packet.hpp"

enum class CacheEntryType
{
    STRING,
    LIST,
    SET,
    MAP,
    SORTED_SET
};

struct CacheEntry
{
    CacheEntryType type=CacheEntryType::STRING;
    std::shared_ptr<std::string> value = nullptr;
    std::shared_ptr<std::vector<std::string>> list_value = nullptr;
    std::shared_ptr<std::set<std::string>> set_value = nullptr;
    std::shared_ptr<std::map<std::string, std::string>> map_value = nullptr;
    std::shared_ptr<std::map<double, std::string>> sorted_set_value = nullptr;

    size_t GetSize() const
    {
        switch (type)
        {
        case CacheEntryType::STRING:
            return (value ? value->size() : 0);
        case CacheEntryType::LIST:
            return (list_value ? list_value->size() : 0);
        case CacheEntryType::SET:
            return (set_value ? set_value->size() : 0);
        case CacheEntryType::MAP:
            return (map_value ? map_value->size() : 0);
        case CacheEntryType::SORTED_SET:
            return (sorted_set_value ? sorted_set_value->size() : 0);
        default:
            return 0;
        }
    }

    std::shared_ptr<Packet> ToRequest(const std::string &key) const
    {
        auto commands = std::make_unique<RESPArray>();

        switch (type)
        {
        case CacheEntryType::STRING:
            commands->value.push_back(std::make_unique<RESPBulkString>(std::make_shared<std::string>("SET")));
            commands->value.push_back(std::make_unique<RESPBulkString>(std::make_shared<std::string>(key)));
            commands->value.push_back(std::make_unique<RESPBulkString>(value));
            break;
        case CacheEntryType::LIST:
            commands->value.push_back(std::make_unique<RESPBulkString>(std::make_shared<std::string>("RPUSH")));
            commands->value.push_back(std::make_unique<RESPBulkString>(std::make_shared<std::string>(key)));
            for (const auto &item : *list_value)
            {
                commands->value.push_back(std::make_unique<RESPBulkString>(std::make_shared<std::string>(item)));
            }
            break;
        case CacheEntryType::SET:
            commands->value.push_back(std::make_unique<RESPBulkString>(std::make_shared<std::string>("SADD")));
            commands->value.push_back(std::make_unique<RESPBulkString>(std::make_shared<std::string>(key)));
            for (const auto &item : *set_value)
            {
                commands->value.push_back(std::make_unique<RESPBulkString>(std::make_shared<std::string>(item)));
            }
            break;
        case CacheEntryType::MAP:
            commands->value.push_back(std::make_unique<RESPBulkString>(std::make_shared<std::string>("HMSET")));
            commands->value.push_back(std::make_unique<RESPBulkString>(std::make_shared<std::string>(key)));
            for (const auto &[field, value] : *map_value)
            {
                commands->value.push_back(std::make_unique<RESPBulkString>(std::make_shared<std::string>(field)));
                commands->value.push_back(std::make_unique<RESPBulkString>(std::make_shared<std::string>(value)));
            }
            break;
        case CacheEntryType::SORTED_SET:
            commands->value.push_back(std::make_unique<RESPBulkString>(std::make_shared<std::string>("ZADD")));
            commands->value.push_back(std::make_unique<RESPBulkString>(std::make_shared<std::string>(key)));
            for (const auto &[member, score] : *sorted_set_value)
            {
                commands->value.push_back(
                    std::make_unique<RESPBulkString>(std::make_shared<std::string>(std::to_string(score))));
                commands->value.push_back(std::make_unique<RESPBulkString>(std::make_shared<std::string>(member)));
            }
            break;
        default:
            break;
        }

        return std::make_shared<Packet>(std::move(commands));
    }

  private:
    inline void AppendBulkString(std::vector<uint8_t> &buffer, const std::string &str) const
    {
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

struct ConnectionInfo
{
    bool is_in_transaction_ = false;
    std::vector<std::shared_ptr<Packet>> transactions_;
};

class Redis
{
    using Cache = lite::Cache<Redis, Packet, Packet, ConnectionInfo, std::string, CacheEntry>;
    using Logger = lite::Logger<Redis, Packet, Packet, ConnectionInfo, std::string, CacheEntry>;

  public:
    std::pair<std::vector<std::shared_ptr<Packet>>, bool> Match(
        const std::shared_ptr<Packet> &resp, ConnectionInfo &conn,
        lite::ThreadSafeQueue<std::pair<std::shared_ptr<Packet>, bool>> &pending_requests) const;

    void NormalUpdate(const std::shared_ptr<Packet> &resp, std::vector<std::shared_ptr<Packet>> requests,
                      ConnectionInfo &conn, Cache *cache);

    void HandleReplayResponse(const std::shared_ptr<Packet> &resp, std::vector<std::shared_ptr<Packet>> requests,
                              ConnectionInfo &conn, Cache *cache);

    Packet EmergencyServe(std::shared_ptr<Packet> req, ConnectionInfo &conn, Cache *cache, Logger *logger);

  private:
    void NormalUpdateImpl(const std::shared_ptr<Packet> &req, Cache *cache, const bool in_transaction = false);

    RESPType *EmergencyServeImpl(std::shared_ptr<Packet> req, ConnectionInfo &conn, Cache *cache, Logger *logger,
                                 const bool in_transaction = false);
};
