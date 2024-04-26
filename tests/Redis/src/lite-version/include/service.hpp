#pragma once

#include <event.h>

#include <core.hpp>
#include <memory>
#include <string>

#include "packet.hpp"

struct CacheEntry
{
    std::shared_ptr<std::string> value = nullptr;
    size_t GetSize() const
    {
        return (value ? value->size() : 0);
    }
};

struct LogEntry
{
    std::shared_ptr<Packet> value;

    std::shared_ptr<std::vector<uint8_t>> Serialize() const
    {
        return value->Serialize();
    }

    lite::DeserializeResult Deserialize(uint8_t *&begin, uint8_t *end)
    {
        return value->Deserialize(begin, end);
    }

    std::shared_ptr<std::vector<uint8_t>> ToRequests() const
    {
        return value->Serialize();
    }
};

struct ConnectionInfo
{
    bool is_in_transaction_ = false;
    std::vector<std::shared_ptr<Packet>> transactions_;
};

class Redis
{
    using Cache = lite::Cache<std::string, CacheEntry>;
    using Logger = lite::Logger<LogEntry>;

  public:
    std::optional<std::vector<std::shared_ptr<Packet>>> Filter(
        const std::shared_ptr<Packet> &resp, ConnectionInfo &conn,
        std::deque<std::shared_ptr<Packet>> &pending_requests) const;

    void NormalUpdate(const std::shared_ptr<Packet> &resp, std::vector<std::shared_ptr<Packet>> requests,
                      ConnectionInfo &conn, Cache &cache);

    Packet EmergencyServe(std::shared_ptr<Packet> p, ConnectionInfo &conn, Cache &cache, Logger &logger);

  private:
    void NormalUpdateImpl(const std::shared_ptr<Packet> &p, Cache &cache, const bool in_transaction = false);

    RESPType *EmergencyServeImpl(std::shared_ptr<Packet> p, ConnectionInfo &conn, Cache &cache, Logger &logger,
                                 const bool in_transaction = false);
};
