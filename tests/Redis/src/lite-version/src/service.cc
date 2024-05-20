#include "service.hpp"
#include "serialize.hpp"

std::pair<std::vector<std::shared_ptr<Packet>>, bool> Redis::Match(
    const std::shared_ptr<Packet> &resp, ConnectionInfo &conn,
    lite::ThreadSafeQueue<std::pair<std::shared_ptr<Packet>, bool>> &pending_requests) const
{
    auto [req, is_not_replay] = pending_requests.front();
    pending_requests.pop_front();
    RESPArray *command = dynamic_cast<RESPArray *>(req->command.get());
    auto opcode_resp = dynamic_cast<RESPBulkString *>(command->value[0].get());
    if (opcode_resp == nullptr)
    {
        std::cerr << "Invalid request\n";
        return std::make_pair(std::vector<std::shared_ptr<Packet>>(), is_not_replay);
    }

    if (!is_not_replay)
        return std::make_pair(std::vector<std::shared_ptr<Packet>>{req}, is_not_replay);

    auto &opcode = opcode_resp->value;
    std::transform(opcode->begin(), opcode->end(), opcode->begin(), [](unsigned char c) { return std::tolower(c); });

    const bool is_error = dynamic_cast<RESPError *>(resp->command.get());

    if (*opcode == "multi")
    {
        if (!is_error)
            conn.is_in_transaction_ = true;
        return std::make_pair(std::vector<std::shared_ptr<Packet>>(), is_not_replay);
    }
    else if (*opcode == "exec")
    {
        return std::make_pair(std::vector<std::shared_ptr<Packet>>{req}, is_not_replay);
    }
    if (conn.is_in_transaction_)
    {
        if (!is_error)
        {
            conn.transactions_.push_back(req);
        } // TODO: do we need to abort the transaction if it's an illegal command
          // or if there are other kinds of errors here?
        return std::make_pair(std::vector<std::shared_ptr<Packet>>(), is_not_replay);
    }
    if (*opcode == "set" || *opcode == "get")
    {
        return std::make_pair(std::vector<std::shared_ptr<Packet>>{req}, is_not_replay);
    }
    else if (*opcode == "ping")
    {
        return std::make_pair(std::vector<std::shared_ptr<Packet>>{req}, is_not_replay);
    }
    else if (*opcode == "incr")
    {
        return std::make_pair(std::vector<std::shared_ptr<Packet>>{req}, is_not_replay);
    }
    else if (*opcode == "lpush" || *opcode == "rpush" || *opcode == "lpop" || *opcode == "rpop")
    {
        return std::make_pair(std::vector<std::shared_ptr<Packet>>{req}, is_not_replay);
    }
    else if (*opcode == "sadd" || *opcode == "spop")
    {
        return std::make_pair(std::vector<std::shared_ptr<Packet>>{req}, is_not_replay);
    }
    else if (*opcode == "zadd" || *opcode == "zpop" || *opcode == "zpopmin")
    {
        return std::make_pair(std::vector<std::shared_ptr<Packet>>{req}, is_not_replay);
    }
    else if (*opcode == "hset" || *opcode == "hget")
    {
        return std::make_pair(std::vector<std::shared_ptr<Packet>>{req}, is_not_replay);
    }
    std::cerr << "Unknown opcode: " << *opcode << std::endl;
    return std::make_pair(std::vector<std::shared_ptr<Packet>>(), is_not_replay);
}

void Redis::NormalUpdate(const std::shared_ptr<Packet> &resp, std::vector<std::shared_ptr<Packet>> requests,
                         ConnectionInfo &conn, Cache *cache)
{
    if (requests.empty())
        return;
    if (conn.is_in_transaction_)
    {
        RESPArray *responses_resp = dynamic_cast<RESPArray *>(resp->command.get());
        if (responses_resp == nullptr)
        {
            std::cerr << "Invalid response for EXEC:";
            auto response_buffer = resp->Serialize();
            for (const auto &c : *response_buffer)
                std::cerr << c;
            std::cerr << std::endl;
            return;
        }
        auto &responses = responses_resp->value;
        if (conn.transactions_.size() != responses.size())
        {
            std::cerr << "Invalid number of responses: trans " << conn.transactions_.size() << " responses "
                      << responses.size() << std::endl;
            return;
        }

        const auto len = responses.size();
        {
            auto cache_lock = cache->TransactionLock();
            for (size_t i = 0; i < len; ++i)
            {
                if (!dynamic_cast<RESPError *>(responses[i].get()))
                    NormalUpdateImpl(conn.transactions_[i], cache, true);
            }
        }

        conn.is_in_transaction_ = false;
        conn.transactions_.clear();
    }
    else
    {
        if (!dynamic_cast<RESPError *>(resp->command.get()))
            NormalUpdateImpl(requests[0], cache);
    }
}

void Redis::NormalUpdateImpl(const std::shared_ptr<Packet> &req, Cache *cache, const bool in_transaction)
{
    std::string_view opcode;
    try
    {
        opcode = req->GetOpcode();
    }
    catch (const std::exception &e)
    {
        const auto buffer = req->Serialize();
        std::cerr << "Unknown opcode: ";
        for (const auto &c : *buffer)
            std::cerr << c;
        std::cerr << std::endl;
    }
    CacheEntry entry;
    if (opcode == "set")
    {
        if (req->GetArgNum() != 2)
        {
            std::cerr << "Invalid number of arguments for set\n";
            return;
        }
        const auto key = dynamic_cast<RESPString *>(req->GetArg(0));
        if (key == nullptr)
        {
            std::cerr << "Invalid argument for set\n";
            return;
        }
        const auto value = dynamic_cast<RESPString *>(req->GetArg(1));
        if (value == nullptr)
        {
            std::cerr << "Invalid argument for set\n";
            return;
        }
        entry.value = value->value;
        cache->Set(*(key->value), entry, in_transaction);
    }
    else if (opcode == "incr")
    {
        if (req->GetArgNum() != 1)
        {
            std::cerr << "Invalid number of arguments for incr\n";
            return;
        }
        const auto key = dynamic_cast<RESPString *>(req->GetArg(0));
        if (key == nullptr)
        {
            std::cerr << "Invalid argument for incr\n";
            return;
        }
        if (cache->Get(*(key->value), entry, in_transaction))
        {
            *entry.value = std::to_string(std::stoll(*entry.value) + 1);
        }
        else
        {
            entry.value = std::make_shared<std::string>("1");
        }
        cache->Set(*(key->value), entry, in_transaction);
    }
    else if (opcode == "hset")
    {
        if (req->GetArgNum() != 3)
        {
            return;
        }
        const auto key = dynamic_cast<RESPString *>(req->GetArg(0));
        const auto field = dynamic_cast<RESPString *>(req->GetArg(1));
        const auto value = dynamic_cast<RESPString *>(req->GetArg(2));
        if (key == nullptr || field == nullptr || value == nullptr)
        {
            return;
        }
        if (cache->Get(*(key->value), entry, in_transaction))
        {
            auto map = std::make_shared<std::map<std::string, std::string>>();
            if (entry.value != nullptr)
            {
                map = stringToMap(*entry.value);
            }
            (*map)[*field->value] = *value->value;
            // serialize map to entry.value
            *(entry.value) = mapToString(map);
        }
        else
        {
            auto map = std::make_shared<std::map<std::string, std::string>>();
            (*map)[*field->value] = *value->value;
            // serialize map to entry.value
            entry.value = std::make_shared<std::string>(mapToString(map));
        }
        if (cache->Set(*(key->value), entry, in_transaction))
        {
            return;
        }
        return;
    }
    else if (opcode != "get" && opcode != "ping" && opcode != "hget")
    { // TODO: update states using get
        std::cerr << "Unknown opcode: " << opcode << std::endl;
    }
}

void Redis::HandleReplayResponse(const std::shared_ptr<Packet> &resp, std::vector<std::shared_ptr<Packet>> requests,
                                 ConnectionInfo &conn, Cache *cache)
{
    auto error_msg = dynamic_cast<RESPError *>(resp->command.get());
    if (error_msg)
    {
        std::cerr << "Received error msg from full during replay: " << error_msg->value << std::endl;
        exit(1); // TODO: handle error
    }
    return;
}

Packet Redis::EmergencyServe(std::shared_ptr<Packet> req, ConnectionInfo &conn, Cache *cache, Logger *logger)
{
    RESPArray *command = dynamic_cast<RESPArray *>(req->command.get());
    auto opcode_resp = dynamic_cast<RESPBulkString *>(command->value[0].get());
    if (opcode_resp == nullptr)
    {
        std::cerr << "Invalid request\n";
        return {};
    }
    auto &opcode = opcode_resp->value;
    std::transform(opcode->begin(), opcode->end(), opcode->begin(), [](unsigned char c) { return std::tolower(c); });

    RESPType *response = nullptr;
    if (conn.is_in_transaction_)
    {
        if (*opcode == "exec")
        {
            if (!logger->EraseConnectionLogs(conn.transactions_.size() + 1))
            {
                std::cerr << "Failed to undo log\n";
                // Two cases that are expected
                // 1) MULTI; switch to emergency; EXEC;
                // 2) switch to emergency; MULTI; REPLAY; EXEC

                // return Packet(std::unique_ptr<RESPType>(new RESPError(
                //     std::make_shared<std::string>("ERR failed to undo log"))));
            }

            auto response_array = new RESPArray;

            {
                auto cache_lock = cache->TransactionLock();
                for (const auto &c : conn.transactions_)
                {
                    response_array->value.emplace_back(EmergencyServeImpl(c, conn, cache, logger, true));
                }
            }

            conn.is_in_transaction_ = false;
            conn.transactions_.clear();
            response = response_array;
        }
        else
        {
            conn.transactions_.push_back(req);
            logger->Log(req);
            response = new RESPSimpleString(std::make_shared<std::string>("QUEUED"));
        }
    }
    else
    {
        response = EmergencyServeImpl(req, conn, cache, logger);
    }
    return Packet(std::unique_ptr<RESPType>(response));
}

RESPType *Redis::EmergencyServeImpl(std::shared_ptr<Packet> req, ConnectionInfo &conn, Cache *cache, Logger *logger,
                                    const bool in_transaction)
{
    std::string_view opcode;
    try
    {
        opcode = req->GetOpcode();
    }
    catch (const std::exception &e)
    {
        const auto buffer = req->Serialize();
        std::cerr << "Unknown opcode: ";
        for (const auto &c : *buffer)
            std::cerr << c;
        std::cerr << std::endl;
    }
    CacheEntry entry;
    if (opcode == "set")
    {
        if (req->GetArgNum() != 2)
        {
            std::cerr << "Invalid number of arguments for set" << std::endl;
            return new RESPError(std::make_shared<std::string>("ERR wrong number of arguments"));
        }
        const auto key = dynamic_cast<RESPString *>(req->GetArg(0));
        if (key == nullptr)
        {
            std::cerr << "Invalid argument for set\n";
            return new RESPError(std::make_shared<std::string>("ERR wrong type of arguments"));
        }
        const auto value = dynamic_cast<RESPString *>(req->GetArg(1));
        if (value == nullptr)
        {
            std::cerr << "Invalid argument for set\n";
            return new RESPError(std::make_shared<std::string>("ERR wrong type of arguments"));
        }
        entry.value = value->value;
        if (cache->Set(*(key->value), entry, in_transaction))
            return new RESPSimpleString(std::make_shared<std::string>("OK"));
    }
    else if (opcode == "get")
    {
        if (req->GetArgNum() != 1)
        {
            std::cerr << "Invalid number of arguments for get" << std::endl;
            return new RESPError(std::make_shared<std::string>("ERR wrong number of arguments"));
        }
        const auto key = dynamic_cast<RESPString *>(req->GetArg(0));
        if (key == nullptr)
        {
            std::cerr << "Invalid argument for get\n";
            return new RESPError(std::make_shared<std::string>("ERR wrong type of arguments"));
        }
        if (cache->Get(*(key->value), entry, in_transaction))
        {
            return new RESPBulkString(entry.value);
        }
        else
        {
            return new RESPBulkString(nullptr);
        }
    }
    else if (opcode == "ping")
    {
        if (req->GetArgNum() == 0)
        {
            return new RESPSimpleString(std::make_shared<std::string>("PONG"));
        }
        else if (req->GetArgNum() == 1)
        {
            const auto arg = dynamic_cast<RESPString *>(req->GetArg(0));
            if (arg == nullptr)
            {
                std::cerr << "Invalid argument for ping\n";
                return new RESPError(std::make_shared<std::string>("ERR wrong type of arguments"));
            }
            return new RESPBulkString(arg->value);
        }
        else
        {
            std::cerr << "Invalid number of arguments for ping" << std::endl;
            return new RESPError(std::make_shared<std::string>("ERR wrong number of arguments"));
        }
    }
    else if (opcode == "multi")
    {
        conn.is_in_transaction_ = true;
        logger->Log(req);
        return new RESPSimpleString(std::make_shared<std::string>("OK"));
    }
    else if (opcode == "incr")
    {
        if (req->GetArgNum() != 1)
        {
            return new RESPError(std::make_shared<std::string>("ERR wrong number of arguments"));
        }
        const auto key = dynamic_cast<RESPString *>(req->GetArg(0));
        if (key == nullptr)
        {
            std::cerr << "Invalid argument for incr\n";
            return new RESPError(std::make_shared<std::string>("ERR wrong type of arguments"));
        }
        if (cache->Get(*(key->value), entry, in_transaction))
        {
            *entry.value = std::to_string(std::stoll(*entry.value) + 1);
        }
        else
        {
            entry.value = std::make_shared<std::string>("1");
        }
        if (cache->Set(*(key->value), entry, in_transaction))
        {
            return new RESPInteger(entry.value);
        }
    }
    else if (opcode == "hset")
    {
        if (req->GetArgNum() != 3)
        {
            return new RESPError(std::make_shared<std::string>("ERR wrong number of arguments"));
        }
        const auto key = dynamic_cast<RESPString *>(req->GetArg(0));
        const auto field = dynamic_cast<RESPString *>(req->GetArg(1));
        const auto value = dynamic_cast<RESPString *>(req->GetArg(2));
        if (key == nullptr || field == nullptr || value == nullptr)
        {
            return new RESPError(std::make_shared<std::string>("ERR wrong type of arguments"));
        }
        if (cache->Get(*(key->value), entry, in_transaction))
        {
            auto map = std::make_shared<std::map<std::string, std::string>>();
            if (entry.value != nullptr)
            {
                map = stringToMap(*entry.value);
            }
            (*map)[*field->value] = *value->value;
            // serialize map to entry.value
            *(entry.value) = mapToString(map);
        }
        else
        {
            auto map = std::make_shared<std::map<std::string, std::string>>();
            (*map)[*field->value] = *value->value;
            // serialize map to entry.value
            entry.value = std::make_shared<std::string>(mapToString(map));
        }
        if (cache->Set(*(key->value), entry, in_transaction))
        {
            return new RESPSimpleString(std::make_shared<std::string>("OK"));
        }
        return new RESPError(std::make_shared<std::string>("ERR failed to set"));
    }
    else if (opcode == "hget")
    {
        if (req->GetArgNum() != 2)
        {
            return new RESPError(std::make_shared<std::string>("ERR wrong number of arguments"));
        }
        const auto key = dynamic_cast<RESPString *>(req->GetArg(0));
        if (key == nullptr)
        {
            return new RESPError(std::make_shared<std::string>("ERR wrong type of arguments"));
        }
        const auto field = dynamic_cast<RESPString *>(req->GetArg(1));
        if (field == nullptr)
        {
            return new RESPError(std::make_shared<std::string>("ERR wrong type of arguments"));
        }
        if (cache->Get(*(key->value), entry, in_transaction))
        {
            if (entry.value)
            {
                auto map = stringToMap(*entry.value);
                auto it = map->find(*field->value);
                if (it != map->end())
                {
                    return new RESPBulkString(std::make_shared<std::string>(it->second));
                }
            }
        }
        return new RESPBulkString(nullptr);
    }
    else if (opcode == "lpush")
    {
        if (req->GetArgNum() < 2)
        {
            return new RESPError(std::make_shared<std::string>("ERR wrong number of arguments"));
        }
        const auto key = dynamic_cast<RESPString *>(req->GetArg(0));
        if (key == nullptr)
        {
            return new RESPError(std::make_shared<std::string>("ERR wrong type of arguments"));
        }
        auto list = std::make_shared<std::list<std::string>>();
        if (cache->Get(*(key->value), entry, in_transaction))
        {
            if (entry.value != nullptr)
            {
                list = stringToList(*entry.value);
            }
            for (size_t i = 1; i < req->GetArgNum(); ++i)
            {
                const auto value = dynamic_cast<RESPString *>(req->GetArg(i));
                if (value == nullptr)
                {
                    return new RESPError(std::make_shared<std::string>("ERR wrong type of arguments"));
                }
                list->push_front(*value->value);
            }
            *(entry.value) = listToString(list);
        }
        else
        {
            for (size_t i = 1; i < req->GetArgNum(); ++i)
            {
                const auto value = dynamic_cast<RESPString *>(req->GetArg(i));
                if (value == nullptr)
                {
                    return new RESPError(std::make_shared<std::string>("ERR wrong type of arguments"));
                }
                list->push_front(*value->value);
            }
            entry.value = std::make_shared<std::string>(listToString(list));
        }
        if (cache->Set(*(key->value), entry, in_transaction))
        {
            return new RESPInteger(list->size());
        }
        return new RESPError(std::make_shared<std::string>("ERR failed to set"));
    }
    else if (opcode == "rpush")
    {
        if (req->GetArgNum() < 2)
        {
            return new RESPError(std::make_shared<std::string>("ERR wrong number of arguments"));
        }
        const auto key = dynamic_cast<RESPString *>(req->GetArg(0));
        if (key == nullptr)
        {
            return new RESPError(std::make_shared<std::string>("ERR wrong type of arguments"));
        }
        auto list = std::make_shared<std::list<std::string>>();
        if (cache->Get(*(key->value), entry, in_transaction))
        {
            if (entry.value != nullptr)
            {
                list = stringToList(*entry.value);
            }
            for (size_t i = 1; i < req->GetArgNum(); ++i)
            {
                const auto value = dynamic_cast<RESPString *>(req->GetArg(i));
                if (value == nullptr)
                {
                    return new RESPError(std::make_shared<std::string>("ERR wrong type of arguments"));
                }
                list->push_back(*value->value);
            }
            *(entry.value) = listToString(list);
        }
        else
        {
            for (size_t i = 1; i < req->GetArgNum(); ++i)
            {
                const auto value = dynamic_cast<RESPString *>(req->GetArg(i));
                if (value == nullptr)
                {
                    return new RESPError(std::make_shared<std::string>("ERR wrong type of arguments"));
                }
                list->push_back(*value->value);
            }
            entry.value = std::make_shared<std::string>(listToString(list));
        }
        if (cache->Set(*(key->value), entry, in_transaction))
        {
            return new RESPInteger(list->size());
        }
        return new RESPError(std::make_shared<std::string>("ERR failed to set"));
    }
    else if (opcode == "lpop")
    {
        if (req->GetArgNum() != 1)
        {
            return new RESPError(std::make_shared<std::string>("ERR wrong number of arguments"));
        }
        const auto key = dynamic_cast<RESPString *>(req->GetArg(0));
        if (key == nullptr)
        {
            return new RESPError(std::make_shared<std::string>("ERR wrong type of arguments"));
        }
        auto list = std::make_shared<std::list<std::string>>();
        if (cache->Get(*(key->value), entry, in_transaction))
        {
            if (entry.value != nullptr)
            {
                list = stringToList(*entry.value);
            }
            if (list->empty())
            {
                return new RESPBulkString(nullptr);
            }
            auto value = list->front();
            list->pop_front();
            *(entry.value) = listToString(list);
            if (cache->Set(*(key->value), entry, in_transaction))
            {
                return new RESPBulkString(std::make_shared<std::string>(value));
            }
            return new RESPError(std::make_shared<std::string>("ERR failed to set"));
        }
        return new RESPBulkString(nullptr);
    }
    else if (opcode == "rpop")
    {
        if (req->GetArgNum() != 1)
        {
            return new RESPError(std::make_shared<std::string>("ERR wrong number of arguments"));
        }
        const auto key = dynamic_cast<RESPString *>(req->GetArg(0));
        if (key == nullptr)
        {
            return new RESPError(std::make_shared<std::string>("ERR wrong type of arguments"));
        }
        auto list = std::make_shared<std::list<std::string>>();
        if (cache->Get(*(key->value), entry, in_transaction))
        {
            if (entry.value != nullptr)
            {
                list = stringToList(*entry.value);
            }
            if (list->empty())
            {
                return new RESPBulkString(nullptr);
            }
            auto value = list->back();
            list->pop_back();
            *(entry.value) = listToString(list);
            if (cache->Set(*(key->value), entry, in_transaction))
            {
                return new RESPBulkString(std::make_shared<std::string>(value));
            }
            return new RESPError(std::make_shared<std::string>("ERR failed to set"));
        }
        return new RESPBulkString(nullptr);
    }
    else if (opcode == "sadd")
    {
        if (req->GetArgNum() < 2)
        {
            return new RESPError(std::make_shared<std::string>("ERR wrong number of arguments"));
        }
        const auto key = dynamic_cast<RESPString *>(req->GetArg(0));
        if (key == nullptr)
        {
            return new RESPError(std::make_shared<std::string>("ERR wrong type of arguments"));
        }
        auto set = std::make_shared<std::set<std::string>>();
        if (cache->Get(*(key->value), entry, in_transaction))
        {
            if (entry.value != nullptr)
            {
                set = stringToSet(*entry.value);
            }
            for (size_t i = 1; i < req->GetArgNum(); ++i)
            {
                const auto value = dynamic_cast<RESPString *>(req->GetArg(i));
                if (value == nullptr)
                {
                    return new RESPError(std::make_shared<std::string>("ERR wrong type of arguments"));
                }
                set->insert(*value->value);
            }
            *(entry.value) = setToString(set);
        }
        else
        {
            for (size_t i = 1; i < req->GetArgNum(); ++i)
            {
                const auto value = dynamic_cast<RESPString *>(req->GetArg(i));
                if (value == nullptr)
                {
                    return new RESPError(std::make_shared<std::string>("ERR wrong type of arguments"));
                }
                set->insert(*value->value);
            }
            entry.value = std::make_shared<std::string>(setToString(set));
        }
        if (cache->Set(*(key->value), entry, in_transaction))
        {
            return new RESPInteger(set->size());
        }
        return new RESPError(std::make_shared<std::string>("ERR failed to set"));
    }
    else if (opcode == "spop")
    {
        if (req->GetArgNum() != 1)
        {
            return new RESPError(std::make_shared<std::string>("ERR wrong number of arguments"));
        }
        const auto key = dynamic_cast<RESPString *>(req->GetArg(0));
        if (key == nullptr)
        {
            return new RESPError(std::make_shared<std::string>("ERR wrong type of arguments"));
        }
        auto set = std::make_shared<std::set<std::string>>();
        if (cache->Get(*(key->value), entry, in_transaction))
        {
            if (entry.value != nullptr)
            {
                set = stringToSet(*entry.value);
            }
            if (set->empty())
            {
                return new RESPBulkString(nullptr);
            }
            auto it = set->begin();
            std::advance(it, rand() % set->size());
            auto value = *it;
            set->erase(it);
            *(entry.value) = setToString(set);
            if (cache->Set(*(key->value), entry, in_transaction))
            {
                return new RESPBulkString(std::make_shared<std::string>(value));
            }
            return new RESPError(std::make_shared<std::string>("ERR failed to set"));
        }
        return new RESPBulkString(nullptr);
    }
    // else if (opcode == "zadd")
    // {
    //     if (req->GetArgNum() < 3 || (req->GetArgNum() - 1) % 2 != 0)
    //     {
    //         return new RESPError(std::make_shared<std::string>("ERR wrong number of arguments"));
    //     }
    //     const auto key = dynamic_cast<RESPString *>(req->GetArg(0));
    //     if (key == nullptr)
    //     {
    //         return new RESPError(std::make_shared<std::string>("ERR wrong type of arguments"));
    //     }
    //     auto zset = std::make_shared<std::set<std::pair<std::string, int>>>();
    //     if (cache->Get(*(key->value), entry, in_transaction))
    //     {
    //         if (entry.value != nullptr)
    //         {
    //             zset = stringToZSet(*entry.value);
    //         }
    //         for (size_t i = 1; i < req->GetArgNum(); i += 2)
    //         {
    //             const auto score = dynamic_cast<RESPString *>(req->GetArg(i));
    //             const auto member = dynamic_cast<RESPString *>(req->GetArg(i + 1));
    //             if (member == nullptr || score == nullptr)
    //             {
    //                 return new RESPError(std::make_shared<std::string>("ERR wrong type of arguments"));
    //             }
    //             zset->insert(std::make_pair(*member->value, std::stoi(*score->value)));
    //         }
    //         *(entry.value) = zSetToString(zset);
    //     }
    //     else
    //     {
    //         for (size_t i = 1; i < req->GetArgNum(); i += 2)
    //         {
    //             const auto score = dynamic_cast<RESPString *>(req->GetArg(i));
    //             const auto member = dynamic_cast<RESPString *>(req->GetArg(i + 1));
    //             if (member == nullptr || score == nullptr)
    //             {
    //                 return new RESPError(std::make_shared<std::string>("ERR wrong type of arguments"));
    //             }
    //             zset->insert(std::make_pair(*member->value, std::stoi(*score->value)));
    //         }
    //         entry.value = std::make_shared<std::string>(zSetToString(zset));
    //     }
    //     if (cache->Set(*(key->value), entry, in_transaction))
    //     {
    //         return new RESPInteger(zset->size());
    //     }
    //     return new RESPError(std::make_shared<std::string>("ERR failed to set"));
    // }
    // else if (opcode == "zpopmin")
    // {
    //     if (req->GetArgNum() != 1)
    //     {
    //         return new RESPError(std::make_shared<std::string>("ERR wrong number of arguments"));
    //     }
    //     const auto key = dynamic_cast<RESPString *>(req->GetArg(0));
    //     if (key == nullptr)
    //     {
    //         return new RESPError(std::make_shared<std::string>("ERR wrong type of arguments"));
    //     }
    //     auto zset = std::make_shared<std::set<std::pair<std::string, int>>>();
    //     if (cache->Get(*(key->value), entry, in_transaction))
    //     {
    //         if (entry.value != nullptr)
    //         {
    //             zset = stringToZSet(*entry.value);
    //         }
    //         if (zset->empty())
    //         {
    //             return new RESPBulkString(nullptr);
    //         }
    //         auto it = zset->begin();
    //         auto value = it->first;
    //         zset->erase(it);
    //         *(entry.value) = zSetToString(zset);
    //         if (cache->Set(*(key->value), entry, in_transaction))
    //         {
    //             return new RESPArray(std::vector<std::shared_ptr<RESPType>>{
    //                 std::make_shared<RESPBulkString>(std::make_shared<std::string>(value)),
    //                 std::make_shared<RESPInteger>(it.second)});
    //         }
    //         return new RESPError(std::make_shared<std::string>("ERR failed to set"));
    //     }
    //     return new RESPBulkString(nullptr);
    // }
    std::cerr << "Unknown opcode: " << opcode << std::endl;
    return new RESPError(std::make_shared<std::string>("ERR unknown command"));
}