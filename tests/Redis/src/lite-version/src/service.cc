#include "service.hpp"

std::shared_ptr<Packet> Redis::abort_req_ = nullptr;

Redis::Redis() {
  if (abort_req_) return;
  abort_req_ = std::make_shared<Packet>();
  auto discard_comm = std::make_unique<RESPArray>();
  discard_comm->value.emplace_back(std::make_unique<RESPBulkString>(
      std::make_shared<std::string>("DISCARD")));
  abort_req_->command = std::move(discard_comm);
}

std::pair<std::vector<std::shared_ptr<Packet>>, bool> Redis::Match(
    const std::shared_ptr<Packet> &resp, ConnectionInfo &conn,
    lite::ThreadSafeQueue<std::pair<std::shared_ptr<Packet>, bool>>
        &pending_requests) const {
  auto [req, is_not_replay] = pending_requests.pop_front();
  RESPArray *command = dynamic_cast<RESPArray *>(req->command.get());
  auto opcode_resp = dynamic_cast<RESPBulkString *>(command->value[0].get());
  if (opcode_resp == nullptr) {
    LOG(ERROR) << "Invalid request\n";
    return std::make_pair(std::vector<std::shared_ptr<Packet>>(),
                          is_not_replay);
  }

  if (!is_not_replay)
    return std::make_pair(std::vector<std::shared_ptr<Packet>>{req},
                          is_not_replay);

  auto &opcode = opcode_resp->value;
  std::transform(opcode->begin(), opcode->end(), opcode->begin(),
                 [](unsigned char c) { return std::tolower(c); });

  const bool is_error = dynamic_cast<RESPError *>(resp->command.get());

  if (*opcode == "multi") {
    if (!is_error) conn.is_in_transaction_ = true;
    return std::make_pair(std::vector<std::shared_ptr<Packet>>(),
                          is_not_replay);
  } else if (*opcode == "exec") {
    return std::make_pair(std::vector<std::shared_ptr<Packet>>{req},
                          is_not_replay);
  }
  if (conn.is_in_transaction_) {
    if (!is_error) {
      conn.transactions_.push_back(req);
    }  // TODO: do we need to abort the transaction if it's an illegal command
       // or if there are other kinds of errors here?
    return std::make_pair(std::vector<std::shared_ptr<Packet>>(),
                          is_not_replay);
  }
  if (*opcode == "set" || *opcode == "get") {
    return std::make_pair(std::vector<std::shared_ptr<Packet>>{req},
                          is_not_replay);
  } else if (*opcode == "ping") {
    return std::make_pair(std::vector<std::shared_ptr<Packet>>{req},
                          is_not_replay);
  } else if (*opcode == "incr") {
    return std::make_pair(std::vector<std::shared_ptr<Packet>>{req},
                          is_not_replay);
  } else if (*opcode == "lpush" || *opcode == "rpush" || *opcode == "lpop" ||
             *opcode == "rpop") {
    return std::make_pair(std::vector<std::shared_ptr<Packet>>{req},
                          is_not_replay);
  } else if (*opcode == "sadd" || *opcode == "spop") {
    return std::make_pair(std::vector<std::shared_ptr<Packet>>{req},
                          is_not_replay);
  } else if (*opcode == "zadd" || *opcode == "zpop" || *opcode == "zpopmin") {
    return std::make_pair(std::vector<std::shared_ptr<Packet>>{req},
                          is_not_replay);
  } else if (*opcode == "hset" || *opcode == "hget") {
    return std::make_pair(std::vector<std::shared_ptr<Packet>>{req},
                          is_not_replay);
  }
  LOG(ERROR) << "Unknown opcode: " << *opcode << std::endl;
  return std::make_pair(std::vector<std::shared_ptr<Packet>>(), is_not_replay);
}

void Redis::NormalUpdate(const std::shared_ptr<Packet> &resp,
                         std::vector<std::shared_ptr<Packet>> requests,
                         ConnectionInfo &conn, Cache *cache) {
  if (requests.empty()) return;
  if (conn.is_in_transaction_) {
    RESPArray *responses_resp = dynamic_cast<RESPArray *>(resp->command.get());
    if (responses_resp == nullptr) {
      LOG(ERROR) << "Invalid response for EXEC:";
      auto response_buffer = resp->Serialize();
      for (const auto &c : *response_buffer) LOG(ERROR) << c;
      LOG(ERROR) << std::endl;
#ifndef NDEBUG
      throw std::runtime_error("Invalid response for EXEC");
#endif
      return;
    }
    auto &responses = responses_resp->value;
    if (conn.transactions_.size() != responses.size()) {
      LOG(ERROR) << "Invalid number of responses: trans "
                 << conn.transactions_.size() << " responses "
                 << responses.size() << std::endl;
      return;
    }

    const auto len = responses.size();
    {
      auto cache_lock = cache->TransactionLock();
      for (size_t i = 0; i < len; i++) {
        if (!dynamic_cast<RESPError *>(responses[i].get()))
          NormalUpdateImpl(conn.transactions_[i], cache, true);
      }
    }

    conn.is_in_transaction_ = false;
    conn.transactions_.clear();
  } else {
    if (!dynamic_cast<RESPError *>(resp->command.get()))
      NormalUpdateImpl(requests[0], cache);
  }
}

void Redis::NormalUpdateImpl(const std::shared_ptr<Packet> &req, Cache *cache,
                             const bool in_transaction) {
  std::string_view opcode;
  try {
    opcode = req->GetOpcode();
  } catch (const std::exception &e) {
    const auto buffer = req->Serialize();
    LOG(ERROR) << "Unknown opcode: ";
    for (const auto &c : *buffer) LOG(ERROR) << c;
    LOG(ERROR) << std::endl;
  }
  CacheEntry entry;
  if (opcode == "set") {
    if (req->GetArgNum() != 2) {
      LOG(ERROR) << "Invalid number of arguments for set\n";
      return;
    }
    const auto key = dynamic_cast<RESPString *>(req->GetArg(0));
    if (key == nullptr) {
      LOG(ERROR) << "Invalid argument for set\n";
      return;
    }
    const auto value = dynamic_cast<RESPString *>(req->GetArg(1));
    if (value == nullptr) {
      LOG(ERROR) << "Invalid argument for set\n";
      return;
    }
    entry.type = CacheEntryType::STRING;
    entry.value = value->value;
    cache->Set(*(key->value), entry, in_transaction);
  } else if (opcode == "incr") {
    if (req->GetArgNum() != 1) {
      LOG(ERROR) << "Invalid number of arguments for incr\n";
      return;
    }
    const auto key = dynamic_cast<RESPString *>(req->GetArg(0));
    if (key == nullptr) {
      LOG(ERROR) << "Invalid argument for incr\n";
      return;
    }
    if (cache->Get(*(key->value), entry, in_transaction)) {
      *entry.value = std::to_string(std::stoll(*entry.value) + 1);
    } else {
      entry.value = std::make_shared<std::string>("1");
    }
    entry.type = CacheEntryType::STRING;
    cache->Set(*(key->value), entry, in_transaction);
  } else if (opcode == "hset") {
    if (req->GetArgNum() != 3) {
      return;
    }
    const auto key = dynamic_cast<RESPString *>(req->GetArg(0));
    const auto field = dynamic_cast<RESPString *>(req->GetArg(1));
    const auto value = dynamic_cast<RESPString *>(req->GetArg(2));
    if (key == nullptr || field == nullptr || value == nullptr) {
      return;
    }
    if (cache->Get(*(key->value), entry, in_transaction)) {
      auto map = std::make_shared<std::map<std::string, std::string>>();
      if (entry.map_value == nullptr) {
        entry.map_value =
            std::make_shared<std::map<std::string, std::string>>();
      }
      map = entry.map_value;
      (*map)[*field->value] = *value->value;
    } else {
      auto map = std::make_shared<std::map<std::string, std::string>>();
      (*map)[*field->value] = *value->value;
      entry.map_value = map;
    }
    entry.type = CacheEntryType::MAP;
    if (cache->Set(*(key->value), entry, in_transaction)) {
      return;
    }
    return;
  } else if (opcode == "lpush") {
    if (req->GetArgNum() < 2) {
      return;
    }
    const auto key = dynamic_cast<RESPString *>(req->GetArg(0));
    if (key == nullptr) {
      return;
    }
    auto list = std::make_shared<std::list<std::string>>();
    if (cache->Get(*(key->value), entry, in_transaction)) {
      if (entry.list_value == nullptr) {
        entry.list_value = list;
      }
      list = entry.list_value;
      for (size_t i = 1; i < req->GetArgNum(); i++) {
        const auto value = dynamic_cast<RESPString *>(req->GetArg(i));
        if (value == nullptr) {
          return;
        }
        list->push_front(*value->value);
      }
    } else {
      for (size_t i = 1; i < req->GetArgNum(); i++) {
        const auto value = dynamic_cast<RESPString *>(req->GetArg(i));
        if (value == nullptr) {
          return;
        }
        list->push_front(*value->value);
      }
      entry.list_value = list;
    }
    entry.type = CacheEntryType::LIST;
    if (cache->Set(*(key->value), entry, in_transaction)) {
      return;
    }
    return;
  } else if (opcode == "rpush") {
    if (req->GetArgNum() < 2) {
      return;
    }
    const auto key = dynamic_cast<RESPString *>(req->GetArg(0));
    if (key == nullptr) {
      return;
    }
    auto list = std::make_shared<std::list<std::string>>();
    if (cache->Get(*(key->value), entry, in_transaction)) {
      if (entry.list_value == nullptr) {
        entry.list_value = std::make_shared<std::list<std::string>>();
      }
      list = entry.list_value;
      for (size_t i = 1; i < req->GetArgNum(); i++) {
        const auto value = dynamic_cast<RESPString *>(req->GetArg(i));
        if (value == nullptr) {
          return;
        }
        list->push_back(*value->value);
      }
    } else {
      for (size_t i = 1; i < req->GetArgNum(); i++) {
        const auto value = dynamic_cast<RESPString *>(req->GetArg(i));
        if (value == nullptr) {
          return;
        }
        list->push_back(*value->value);
      }
      entry.list_value = list;
    }
    entry.type = CacheEntryType::LIST;
    if (cache->Set(*(key->value), entry, in_transaction)) {
      return;
    }
    return;
  } else if (opcode == "lpop") {
    if (req->GetArgNum() != 1) {
      return;
    }
    const auto key = dynamic_cast<RESPString *>(req->GetArg(0));
    if (key == nullptr) {
      return;
    }
    if (cache->Get(*(key->value), entry, in_transaction)) {
      if (entry.list_value == nullptr) {
        return;
      }
      if (entry.list_value->empty()) {
        return;
      }
      entry.list_value->pop_front();
      entry.type = CacheEntryType::LIST;
    }
    if (cache->Set(*(key->value), entry, in_transaction)) {
      return;
    }
    return;
  } else if (opcode == "rpop") {
    if (req->GetArgNum() != 1) {
      return;
    }
    const auto key = dynamic_cast<RESPString *>(req->GetArg(0));
    if (key == nullptr) {
      return;
    }
    if (cache->Get(*(key->value), entry, in_transaction)) {
      if (entry.list_value == nullptr) {
        return;
      }
      if (entry.list_value->empty()) {
        return;
      }
      entry.list_value->pop_back();
      entry.type = CacheEntryType::LIST;
    }
    if (cache->Set(*(key->value), entry, in_transaction)) {
      return;
    }
    return;
  } else if (opcode == "sadd") {
    if (req->GetArgNum() < 2) {
      return;
    }
    const auto key = dynamic_cast<RESPString *>(req->GetArg(0));
    if (key == nullptr) {
      return;
    }
    auto set = std::make_shared<std::set<std::string>>();
    if (cache->Get(*(key->value), entry, in_transaction)) {
      if (entry.set_value == nullptr) {
        entry.set_value = std::make_shared<std::set<std::string>>();
      }
      set = entry.set_value;
      for (size_t i = 1; i < req->GetArgNum(); i++) {
        const auto value = dynamic_cast<RESPString *>(req->GetArg(i));
        if (value == nullptr) {
          return;
        }
        set->insert(*value->value);
      }
    } else {
      for (size_t i = 1; i < req->GetArgNum(); i++) {
        const auto value = dynamic_cast<RESPString *>(req->GetArg(i));
        if (value == nullptr) {
          return;
        }
        set->insert(*value->value);
      }
      entry.set_value = set;
    }
    entry.type = CacheEntryType::SET;
    if (cache->Set(*(key->value), entry, in_transaction)) {
      return;
    }
    return;
  } else if (opcode == "spop") {
    if (req->GetArgNum() != 1) {
      return;
    }
    const auto key = dynamic_cast<RESPString *>(req->GetArg(0));
    if (key == nullptr) {
      return;
    }
    auto set = std::make_shared<std::set<std::string>>();
    if (cache->Get(*(key->value), entry, in_transaction)) {
      if (entry.set_value == nullptr) {
        return;
      }
      if (entry.set_value->empty()) {
        return;
      }
      auto it = entry.set_value->begin();
      std::advance(it, rand() % set->size());
      auto value = *it;
      entry.set_value->erase(it);
      entry.type = CacheEntryType::SET;
      if (cache->Set(*(key->value), entry, in_transaction)) {
        return;
      }
      return;
    }
    return;
  } else if (opcode == "zadd") {
    if (req->GetArgNum() < 3 || (req->GetArgNum() - 1) % 2 != 0) {
      return;
    }
    const auto key = dynamic_cast<RESPString *>(req->GetArg(0));
    if (key == nullptr) {
      return;
    }
    auto zset = std::make_shared<std::map<double, std::string>>();
    if (cache->Get(*(key->value), entry, in_transaction)) {
      if (entry.sorted_set_value == nullptr) {
        entry.sorted_set_value =
            std::make_shared<std::map<double, std::string>>();
      }
      zset = entry.sorted_set_value;
      for (size_t i = 1; i < req->GetArgNum(); i += 2) {
        const auto score = dynamic_cast<RESPString *>(req->GetArg(i));
        const auto value = dynamic_cast<RESPString *>(req->GetArg(i + 1));
        if (score == nullptr || value == nullptr) {
          return;
        }
        (*zset)[std::stod(*score->value)] = *value->value;
      }
      entry.type = CacheEntryType::ZSET;
      if (cache->Set(*(key->value), entry, in_transaction)) {
        return;
      }
      return;
    }
  } else if (opcode == "zpopmin") {
    if (req->GetArgNum() != 1) {
      return;
    }
    const auto key = dynamic_cast<RESPString *>(req->GetArg(0));
    if (key == nullptr) {
      return;
    }
    auto zset = std::make_shared<std::set<std::pair<std::string, int>>>();
    if (cache->Get(*(key->value), entry, in_transaction)) {
      if (entry.sorted_set_value == nullptr) {
        return;
      }
      if (entry.sorted_set_value->empty()) {
        return;
      }
      auto it = entry.sorted_set_value->begin();
      auto value = it->second;
      entry.sorted_set_value->erase(it);
      entry.type = CacheEntryType::ZSET;
      if (cache->Set(*(key->value), entry, in_transaction)) {
        return;
      }
    }
    return;
  } else if (opcode != "get" && opcode != "ping" &&
             opcode != "hget") {  // TODO: update states using get
    LOG(ERROR) << "Unknown opcode: " << opcode << std::endl;
  }
}

void Redis::HandleReplayResponse(const std::shared_ptr<Packet> &resp,
                                 std::vector<std::shared_ptr<Packet>> requests,
                                 ConnectionInfo &conn, Cache *cache) {
  auto error_msg = dynamic_cast<RESPError *>(resp->command.get());
  if (error_msg) {
    LOG(ERROR) << "Received error msg from full during replay: "
               << *error_msg->value << std::endl;
    exit(1);  // TODO: handle error
  }
  return;
}

std::pair<Packet, bool> Redis::EmergencyServe(std::shared_ptr<Packet> req,
                                              ConnectionInfo &conn,
                                              Cache *cache, Logger *logger,
                                              bool flow_control) {
  bool shutdown = false;
  RESPArray *command = dynamic_cast<RESPArray *>(req->command.get());
  auto opcode_resp = dynamic_cast<RESPBulkString *>(command->value[0].get());
  if (opcode_resp == nullptr) {
    LOG(ERROR) << "Invalid request\n";
    return {};
  }
  auto &opcode = opcode_resp->value;
  std::transform(opcode->begin(), opcode->end(), opcode->begin(),
                 [](unsigned char c) { return std::tolower(c); });

  RESPType *response = nullptr;
  if (conn.is_in_transaction_) {
    if (*opcode == "exec") {
      if (!logger->EraseConnectionLogs(conn.transactions_.size() + 1)) {
        LOG(ERROR) << "Failed to undo log\n";
        logger->Log(abort_req_);
      }

      auto response_array = new RESPArray;
      if (flow_control) {
        response_array->value.emplace_back(
            new RESPError(std::make_shared<std::string>("ERR flow control")));
        shutdown = true;
      } else {
        auto cache_lock = cache->TransactionLock();
        for (const auto &c : conn.transactions_) {
          response_array->value.emplace_back(
              EmergencyServeImpl(c, conn, cache, logger, false, true).first);
        }
      }

      conn.is_in_transaction_ = false;
      conn.transactions_.clear();
      response = response_array;
    } else {
      if (flow_control) {
        response = new RESPError(
            std::make_shared<std::string>("ERR flow control enabled"));
        if (!logger->EraseConnectionLogs(conn.transactions_.size() + 1)) {
          LOG(ERROR) << "Failed to undo log\n";
          logger->Log(abort_req_);
        }
        conn.transactions_.clear();
        shutdown = true;
      } else {
        conn.transactions_.push_back(req);
        logger->Log(req);
        response =
            new RESPSimpleString(std::make_shared<std::string>("QUEUED"));
      }
    }
  } else {
    std::tie(response, shutdown) =
        EmergencyServeImpl(req, conn, cache, logger, flow_control);
  }
  return {Packet(std::unique_ptr<RESPType>(response)), shutdown};
}

std::pair<RESPType *, bool> Redis::EmergencyServeImpl(
    std::shared_ptr<Packet> req, ConnectionInfo &conn, Cache *cache,
    Logger *logger, bool flow_control, const bool in_transaction) {
  std::string_view opcode;
  try {
    opcode = req->GetOpcode();
  } catch (const std::exception &e) {
    const auto buffer = req->Serialize();
    LOG(ERROR) << "Unknown opcode: ";
    for (const auto &c : *buffer) LOG(ERROR) << c;
    LOG(ERROR) << std::endl;
  }
  CacheEntry entry;
  if (opcode == "set") {
    if (req->GetArgNum() != 2) {
      LOG(ERROR) << "Invalid number of arguments for set" << std::endl;
      return {new RESPError(std::make_shared<std::string>(
                  "ERR wrong number of arguments")),
              false};
    }
    const auto key = dynamic_cast<RESPString *>(req->GetArg(0));
    if (key == nullptr) {
      LOG(ERROR) << "Invalid argument for set\n";
      return {new RESPError(
                  std::make_shared<std::string>("ERR wrong type of arguments")),
              false};
    }
    const auto value = dynamic_cast<RESPString *>(req->GetArg(1));
    if (value == nullptr) {
      LOG(ERROR) << "Invalid argument for set\n";
      return {new RESPError(
                  std::make_shared<std::string>("ERR wrong type of arguments")),
              false};
    }
    if (flow_control) {
      return {new RESPError(
                  std::make_shared<std::string>("ERR flow control enabled")),
              false};  // no need to close connection here, so that the client
                       // can reuse it in the future
    }
    entry.value = value->value;
    entry.type = CacheEntryType::STRING;
    if (cache->Set(*(key->value), entry, in_transaction))
      return {new RESPSimpleString(std::make_shared<std::string>("OK")), false};
  } else if (opcode == "get") {
    if (req->GetArgNum() != 1) {
      LOG(ERROR) << "Invalid number of arguments for get" << std::endl;
      return {new RESPError(std::make_shared<std::string>(
                  "ERR wrong number of arguments")),
              false};
    }
    const auto key = dynamic_cast<RESPString *>(req->GetArg(0));
    if (key == nullptr) {
      LOG(ERROR) << "Invalid argument for get\n";
      return {new RESPError(
                  std::make_shared<std::string>("ERR wrong type of arguments")),
              false};
    }
    if (cache->Get(*(key->value), entry, in_transaction)) {
      return {new RESPBulkString(entry.value), false};
    } else {
      return {new RESPBulkString(nullptr), false};
    }
  } else if (opcode == "ping") {
    if (req->GetArgNum() == 0) {
      return {new RESPSimpleString(std::make_shared<std::string>("PONG")),
              false};
    } else if (req->GetArgNum() == 1) {
      const auto arg = dynamic_cast<RESPString *>(req->GetArg(0));
      if (arg == nullptr) {
        LOG(ERROR) << "Invalid argument for ping\n";
        return {new RESPError(std::make_shared<std::string>(
                    "ERR wrong type of arguments")),
                false};
      }
      return {new RESPBulkString(arg->value), false};
    } else {
      LOG(ERROR) << "Invalid number of arguments for ping" << std::endl;
      return {new RESPError(std::make_shared<std::string>(
                  "ERR wrong number of arguments")),
              false};
    }
  } else if (opcode == "multi") {
    if (flow_control) {
      return {new RESPError(
                  std::make_shared<std::string>("ERR flow control enabled")),
              true};
    }
    conn.is_in_transaction_ = true;
    logger->Log(req);
    return {new RESPSimpleString(std::make_shared<std::string>("OK")), false};
  } else if (opcode == "incr") {
    if (req->GetArgNum() != 1) {
      return {new RESPError(std::make_shared<std::string>(
                  "ERR wrong number of arguments")),
              false};
    }
    const auto key = dynamic_cast<RESPString *>(req->GetArg(0));
    if (key == nullptr) {
      LOG(ERROR) << "Invalid argument for incr\n";
      return {new RESPError(
                  std::make_shared<std::string>("ERR wrong type of arguments")),
              false};
    }
    if (cache->Get(*(key->value), entry, in_transaction)) {
      *entry.value = std::to_string(std::stoll(*entry.value) + 1);
    } else {
      entry.value = std::make_shared<std::string>("1");
    }
    entry.type = CacheEntryType::STRING;
    if (cache->Set(*(key->value), entry, in_transaction)) {
      return {new RESPInteger(entry.value), false};
    }
  } else if (opcode == "hset") {
    if (req->GetArgNum() != 3) {
      return {new RESPError(std::make_shared<std::string>(
                  "ERR wrong number of arguments")),
              false};
    }
    const auto key = dynamic_cast<RESPString *>(req->GetArg(0));
    const auto field = dynamic_cast<RESPString *>(req->GetArg(1));
    const auto value = dynamic_cast<RESPString *>(req->GetArg(2));
    if (key == nullptr || field == nullptr || value == nullptr) {
      return {new RESPError(
                  std::make_shared<std::string>("ERR wrong type of arguments")),
              false};
    }
    if (cache->Get(*(key->value), entry, in_transaction)) {
      auto map = std::make_shared<std::map<std::string, std::string>>();
      if (entry.map_value == nullptr) {
        entry.map_value =
            std::make_shared<std::map<std::string, std::string>>();
      }
      map = entry.map_value;
      (*map)[*field->value] = *value->value;
    } else {
      auto map = std::make_shared<std::map<std::string, std::string>>();
      (*map)[*field->value] = *value->value;
      entry.map_value = map;
    }
    entry.type = CacheEntryType::MAP;
    if (cache->Set(*(key->value), entry, in_transaction)) {
      return {new RESPSimpleString(std::make_shared<std::string>("OK")), false};
    }
    return {new RESPError(std::make_shared<std::string>("ERR failed to set")),
            false};
  } else if (opcode == "hget") {
    if (req->GetArgNum() != 2) {
      return {new RESPError(std::make_shared<std::string>(
                  "ERR wrong number of arguments")),
              false};
    }
    const auto key = dynamic_cast<RESPString *>(req->GetArg(0));
    if (key == nullptr) {
      return {new RESPError(
                  std::make_shared<std::string>("ERR wrong type of arguments")),
              false};
    }
    const auto field = dynamic_cast<RESPString *>(req->GetArg(1));
    if (field == nullptr) {
      return {new RESPError(
                  std::make_shared<std::string>("ERR wrong type of arguments")),
              false};
    }
    if (cache->Get(*(key->value), entry, in_transaction)) {
      if (entry.map_value != nullptr) {
        auto it = entry.map_value->find(*field->value);
        if (it != entry.map_value->end()) {
          return {new RESPBulkString(std::make_shared<std::string>(it->second)),
                  false};
        }
      }
    }
    return {new RESPBulkString(nullptr), false};
  } else if (opcode == "lpush") {
    if (req->GetArgNum() < 2) {
      return {new RESPError(std::make_shared<std::string>(
                  "ERR wrong number of arguments")),
              false};
    }
    const auto key = dynamic_cast<RESPString *>(req->GetArg(0));
    if (key == nullptr) {
      return {new RESPError(
                  std::make_shared<std::string>("ERR wrong type of arguments")),
              false};
    }
    auto list = std::make_shared<std::list<std::string>>();
    if (cache->Get(*(key->value), entry, in_transaction)) {
      if (entry.list_value != nullptr) {
        list = entry.list_value;
      }
      for (size_t i = 1; i < req->GetArgNum(); i++) {
        const auto value = dynamic_cast<RESPString *>(req->GetArg(i));
        if (value == nullptr) {
          return {new RESPError(std::make_shared<std::string>(
                      "ERR wrong type of arguments")),
                  false};
        }
        list->push_front(*value->value);
      }
    } else {
      for (size_t i = 1; i < req->GetArgNum(); i++) {
        const auto value = dynamic_cast<RESPString *>(req->GetArg(i));
        if (value == nullptr) {
          return {new RESPError(std::make_shared<std::string>(
                      "ERR wrong type of arguments")),
                  false};
        }
        list->push_front(*value->value);
      }
    }
    entry.list_value = list;
    entry.type = CacheEntryType::LIST;
    if (cache->Set(*(key->value), entry, in_transaction)) {
      return {new RESPInteger(
                  std::make_shared<std::string>(std::to_string(list->size()))),
              false};
    }
  } else if (opcode == "rpush") {
    if (req->GetArgNum() < 2) {
      return {new RESPError(std::make_shared<std::string>(
                  "ERR wrong number of arguments")),
              false};
    }
    const auto key = dynamic_cast<RESPString *>(req->GetArg(0));
    if (key == nullptr) {
      return {new RESPError(
                  std::make_shared<std::string>("ERR wrong type of arguments")),
              false};
    }
    auto list = std::make_shared<std::list<std::string>>();
    if (cache->Get(*(key->value), entry, in_transaction)) {
      if (entry.list_value != nullptr) {
        list = entry.list_value;
      }
      for (size_t i = 1; i < req->GetArgNum(); i++) {
        const auto value = dynamic_cast<RESPString *>(req->GetArg(i));
        if (value == nullptr) {
          return {new RESPError(std::make_shared<std::string>(
                      "ERR wrong type of arguments")),
                  false};
        }
        list->push_back(*value->value);
      }
    } else {
      for (size_t i = 1; i < req->GetArgNum(); i++) {
        const auto value = dynamic_cast<RESPString *>(req->GetArg(i));
        if (value == nullptr) {
          return {new RESPError(std::make_shared<std::string>(
                      "ERR wrong type of arguments")),
                  false};
        }
        list->push_back(*value->value);
      }
      entry.list_value = list;
    }
    entry.type = CacheEntryType::LIST;
    if (cache->Set(*(key->value), entry, in_transaction)) {
      return {new RESPInteger(list->size()), false};
    }
    return {new RESPError(std::make_shared<std::string>("ERR failed to set")),
            false};
  } else if (opcode == "lpop") {
    if (req->GetArgNum() != 1) {
      return {new RESPError(std::make_shared<std::string>(
                  "ERR wrong number of arguments")),
              false};
    }
    const auto key = dynamic_cast<RESPString *>(req->GetArg(0));
    if (key == nullptr) {
      return {new RESPError(
                  std::make_shared<std::string>("ERR wrong type of arguments")),
              false};
    }
    auto list = std::make_shared<std::list<std::string>>();
    if (cache->Get(*(key->value), entry, in_transaction)) {
      if (entry.list_value != nullptr) {
        list = entry.list_value;
      } else {
        return {new RESPBulkString(nullptr), false};
      }
      if (list->empty()) {
        return {new RESPBulkString(nullptr), false};
      }
      auto value = list->front();
      list->pop_front();
      if (cache->Set(*(key->value), entry, in_transaction)) {
        return {new RESPBulkString(std::make_shared<std::string>(value)),
                false};
      }
      return {new RESPError(std::make_shared<std::string>("ERR failed to set")),
              false};
    }
    return {new RESPBulkString(nullptr), false};
  } else if (opcode == "rpop") {
    if (req->GetArgNum() != 1) {
      return {new RESPError(std::make_shared<std::string>(
                  "ERR wrong number of arguments")),
              false};
    }
    const auto key = dynamic_cast<RESPString *>(req->GetArg(0));
    if (key == nullptr) {
      return {new RESPError(
                  std::make_shared<std::string>("ERR wrong type of arguments")),
              false};
    }
    auto list = std::make_shared<std::list<std::string>>();
    if (cache->Get(*(key->value), entry, in_transaction)) {
      if (entry.list_value != nullptr) {
        list = entry.list_value;
      } else {
        return {new RESPBulkString(nullptr), false};
      }
      if (list->empty()) {
        return {new RESPBulkString(nullptr), false};
      }
      auto value = list->back();
      list->pop_back();
      if (cache->Set(*(key->value), entry, in_transaction)) {
        return {new RESPBulkString(std::make_shared<std::string>(value)),
                false};
      }
      return {new RESPError(std::make_shared<std::string>("ERR failed to set")),
              false};
    }
    return {new RESPBulkString(nullptr), false};
  } else if (opcode == "sadd") {
    if (req->GetArgNum() < 2) {
      return {new RESPError(std::make_shared<std::string>(
                  "ERR wrong number of arguments")),
              false};
    }
    const auto key = dynamic_cast<RESPString *>(req->GetArg(0));
    if (key == nullptr) {
      return {new RESPError(
                  std::make_shared<std::string>("ERR wrong type of arguments")),
              false};
    }
    auto set = std::make_shared<std::set<std::string>>();
    if (cache->Get(*(key->value), entry, in_transaction)) {
      if (entry.set_value != nullptr) {
        set = entry.set_value;
      } else {
        return {new RESPError(
                    std::make_shared<std::string>("ERR wrong type of object")),
                false};
      }
      for (size_t i = 1; i < req->GetArgNum(); i++) {
        const auto value = dynamic_cast<RESPString *>(req->GetArg(i));
        if (value == nullptr) {
          return {new RESPError(std::make_shared<std::string>(
                      "ERR wrong type of arguments")),
                  false};
        }
        set->insert(*value->value);
      }
    } else {
      for (size_t i = 1; i < req->GetArgNum(); i++) {
        const auto value = dynamic_cast<RESPString *>(req->GetArg(i));
        if (value == nullptr) {
          return {new RESPError(std::make_shared<std::string>(
                      "ERR wrong type of arguments")),
                  false};
        }
        set->insert(*value->value);
      }
      entry.set_value = set;
    }
    entry.type = CacheEntryType::SET;
    if (cache->Set(*(key->value), entry, in_transaction)) {
      return {new RESPInteger(set->size()), false};
    }
    return {new RESPError(std::make_shared<std::string>("ERR failed to set")),
            false};
  } else if (opcode == "spop") {
    if (req->GetArgNum() != 1) {
      return {new RESPError(std::make_shared<std::string>(
                  "ERR wrong number of arguments")),
              false};
    }
    const auto key = dynamic_cast<RESPString *>(req->GetArg(0));
    if (key == nullptr) {
      return {new RESPError(
                  std::make_shared<std::string>("ERR wrong type of arguments")),
              false};
    }
    auto set = std::make_shared<std::set<std::string>>();
    if (cache->Get(*(key->value), entry, in_transaction)) {
      if (entry.set_value != nullptr) {
        set = entry.set_value;
      } else {
        return {new RESPBulkString(nullptr), false};
      }
      if (set->empty()) {
        return {new RESPBulkString(nullptr), false};
      }
      auto it = set->begin();
      std::advance(it, rand() % set->size());
      auto value = *it;
      set->erase(it);
      if (cache->Set(*(key->value), entry, in_transaction)) {
        return {new RESPBulkString(std::make_shared<std::string>(value)),
                false};
      }
      return {new RESPError(std::make_shared<std::string>("ERR failed to set")),
              false};
    }
    return {new RESPBulkString(nullptr), false};
  } else if (opcode == "zadd") {
    if (req->GetArgNum() < 3 || (req->GetArgNum() - 1) % 2 != 0) {
      return {new RESPError(std::make_shared<std::string>(
                  "ERR wrong number of arguments")),
              false};
    }
    const auto key = dynamic_cast<RESPString *>(req->GetArg(0));
    if (key == nullptr) {
      return {new RESPError(
                  std::make_shared<std::string>("ERR wrong type of arguments")),
              false};
    }
    if (cache->Get(*(key->value), entry, in_transaction)) {
      if (entry.type != CacheEntryType::ZSET) {
        return {new RESPError(std::make_shared<std::string>(
                    "ERR value is not a sorted set")),
                false};
      }
      if (entry.sorted_set_value == nullptr) {
        entry.sorted_set_value =
            std::make_shared<std::map<double, std::string>>();
      }
      for (size_t i = 1; i < req->GetArgNum(); i += 2) {
        const auto score = dynamic_cast<RESPString *>(req->GetArg(i));
        const auto member = dynamic_cast<RESPString *>(req->GetArg(i + 1));
        if (member == nullptr || score == nullptr) {
          return {new RESPError(std::make_shared<std::string>(
                      "ERR wrong type of arguments")),
                  false};
        }
        entry.sorted_set_value->insert(
            std::make_pair(std::stod(*score->value), *member->value));
      }
    } else {
      entry.type = CacheEntryType::ZSET;
      entry.sorted_set_value =
          std::make_shared<std::map<double, std::string>>();
      for (size_t i = 1; i < req->GetArgNum(); i += 2) {
        const auto score = dynamic_cast<RESPString *>(req->GetArg(i));
        const auto member = dynamic_cast<RESPString *>(req->GetArg(i + 1));
        if (member == nullptr || score == nullptr) {
          return {new RESPError(std::make_shared<std::string>(
                      "ERR wrong type of arguments")),
                  false};
        }
        entry.sorted_set_value->insert(
            std::make_pair(std::stod(*score->value), *member->value));
      }
    }
    if (cache->Set(*(key->value), entry, in_transaction)) {
      return {new RESPInteger(entry.sorted_set_value->size()), false};
    }
    return {new RESPError(std::make_shared<std::string>("ERR failed to set")),
            false};
  } else if (opcode == "zpopmin") {
    if (req->GetArgNum() != 1) {
      return {new RESPError(std::make_shared<std::string>(
                  "ERR wrong number of arguments")),
              false};
    }
    const auto key = dynamic_cast<RESPString *>(req->GetArg(0));
    if (key == nullptr) {
      return {new RESPError(
                  std::make_shared<std::string>("ERR wrong type of arguments")),
              false};
    }
    auto zset = std::make_shared<std::map<double, std::string>>();
    if (cache->Get(*(key->value), entry, in_transaction)) {
      if (entry.sorted_set_value != nullptr) {
        zset = entry.sorted_set_value;
      } else {
        return {new RESPBulkString(nullptr), false};
      }
      if (zset->empty()) {
        return {new RESPBulkString(nullptr), false};
      }
      auto it = zset->begin();
      auto value = it->second;
      zset->erase(it);
      if (cache->Set(*(key->value), entry, in_transaction)) {
        return {new RESPBulkString(std::make_shared<std::string>(value)),
                false};
      }
      return {new RESPError(std::make_shared<std::string>("ERR failed to pop")),
              false};
    }
    return {new RESPBulkString(nullptr), false};
  }
  LOG(ERROR) << "Unknown opcode: " << opcode << std::endl;
  return {new RESPError(std::make_shared<std::string>("ERR unknown command")),
          false};
}