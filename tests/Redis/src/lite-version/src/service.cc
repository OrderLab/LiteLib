#include "service.hpp"

#include <unordered_set>

ShmSharedPtr<Packet> Redis::abort_req_;

SharedMemory *shm;

void Redis::DelayedConstructor() {
  ShmUniquePtrWithDeleter<RESPArray, RESPTypeDeleter> discard_comm(
      shm->get_segment_manager()->template construct<RESPArray>(
          bip::anonymous_instance)(),
      RESPTypeDeleter{shm->get_segment_manager()});
  discard_comm->value.emplace_back(
      shm->get_segment_manager()->template construct<RESPBulkString>(
          bip::anonymous_instance)(ShmMakeShared(
          shm->get_segment_manager()->template construct<ShmString>(
              bip::anonymous_instance)("DISCARD", shm->get_segment_manager()),
          *shm)),
      RESPTypeDeleter{shm->get_segment_manager()});
  abort_req_ =
      ShmMakeShared(shm->get_segment_manager()->template construct<Packet>(
                        bip::anonymous_instance)(boost::move(discard_comm)),
                    *shm);
}

std::pair<std::vector<ShmSharedPtr<Packet>>, bool> Redis::Match(
    const ShmSharedPtr<Packet> &resp, ConnectionInfo &conn,
    lite::ShmThreadSafeQueue<bip::pair<ShmSharedPtr<Packet>, bool>>
        &pending_requests) const {
  auto [req, is_not_replay] = pending_requests.pop_front();
  RESPArray *command = dynamic_cast<RESPArray *>(req->command.get());
  auto opcode_resp = dynamic_cast<RESPBulkString *>(command->value[0].get());
  if (opcode_resp == nullptr) {
    LOG(ERROR) << "Invalid request\n";
    return std::make_pair(std::vector<ShmSharedPtr<Packet>>(), is_not_replay);
  }

  if (!is_not_replay)
    return std::make_pair(std::vector<ShmSharedPtr<Packet>>{req},
                          is_not_replay);

  auto &opcode = opcode_resp->value;
  std::transform(opcode->begin(), opcode->end(), opcode->begin(),
                 [](unsigned char c) { return std::tolower(c); });

  const bool is_error = dynamic_cast<RESPError *>(resp->command.get());

  if (*opcode == "multi") {
    if (!is_error) conn.is_in_transaction_ = true;
    return std::make_pair(std::vector<ShmSharedPtr<Packet>>(), is_not_replay);
  } else if (*opcode == "exec") {
    return std::make_pair(std::vector<ShmSharedPtr<Packet>>{req},
                          is_not_replay);
  }
  if (conn.is_in_transaction_) {
    if (!is_error) {
      conn.transactions_.push_back(req);
    }  // TODO: do we need to abort the transaction if it's an illegal command
       // or if there are other kinds of errors here?
    return std::make_pair(std::vector<ShmSharedPtr<Packet>>(), is_not_replay);
  }
  static const std::unordered_set<std::string> valid_opcodes = {
      "set",     "get",  "ping", "incr",    "lpush", "rpush",
      "lpop",    "rpop", "sadd", "spop",    "zadd",  "zpop",
      "zpopmin", "hset", "hget", "hgetall", "hmset", "quit"};
  if (valid_opcodes.find(opcode->c_str()) != valid_opcodes.end()) {
    return {{req}, is_not_replay};
  }
  LOG(ERROR) << "Unknown opcode: " << *opcode << std::endl;
  return std::make_pair(std::vector<ShmSharedPtr<Packet>>(), is_not_replay);
}

void Redis::NormalUpdate(const ShmSharedPtr<Packet> &resp,
                         std::vector<ShmSharedPtr<Packet>> requests,
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

void Redis::NormalUpdateImpl(const ShmSharedPtr<Packet> &req, Cache *cache,
                             const bool in_transaction) {
  ConnectionInfo conn(shm->get_segment_manager());
  HandleUpdate(req, conn, cache, NULL, false, in_transaction, false);
}

void Redis::HandleReplayResponse(const ShmSharedPtr<Packet> &resp,
                                 std::vector<ShmSharedPtr<Packet>> requests,
                                 ConnectionInfo &conn, Cache *cache) {
  auto error_msg = dynamic_cast<RESPError *>(resp->command.get());
  if (error_msg) {
    LOG(ERROR) << "Received error msg from full during replay: "
               << *error_msg->value << std::endl;
    exit(1);  // TODO: handle error
  }
  return;
}

std::pair<Packet, bool> Redis::EmergencyServe(ShmSharedPtr<Packet> req,
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

  ShmUniquePtrWithDeleter<RESPType, RESPTypeDeleter> response(
      nullptr, RESPTypeDeleter{shm->get_segment_manager()});
  if (conn.is_in_transaction_) {
    if (*opcode == "exec") {
      if (!logger->EraseConnectionLogs(conn.transactions_.size() + 1)) {
        LOG(ERROR) << "Failed to undo log\n";
        logger->Log(abort_req_);
      }

      auto response_array =
          shm->get_segment_manager()->template construct<RESPArray>(
              bip::anonymous_instance)();
      if (flow_control) {
        response_array->value.emplace_back(
            shm->get_segment_manager()->template construct<RESPError>(
                bip::anonymous_instance)(ShmMakeShared(
                shm->get_segment_manager()->template construct<ShmString>(
                    bip::anonymous_instance)("ERR flow control",
                                             shm->get_segment_manager()),
                *shm)),
            RESPTypeDeleter{shm->get_segment_manager()});
        shutdown = true;
      } else {
        auto cache_lock = cache->TransactionLock();
        for (const auto &c : conn.transactions_) {
          response_array->value.emplace_back(
              EmergencyServeImpl(c, conn, cache, logger, false, true).first,
              RESPTypeDeleter{shm->get_segment_manager()});
        }
      }

      conn.is_in_transaction_ = false;
      conn.transactions_.clear();
      response = {response_array, RESPTypeDeleter{shm->get_segment_manager()}};
    } else {
      if (flow_control) {
        auto error_response =
            shm->get_segment_manager()->template construct<RESPError>(
                bip::anonymous_instance)(ShmMakeShared(
                shm->get_segment_manager()->template construct<ShmString>(
                    bip::anonymous_instance)("ERR flow control enabled",
                                             shm->get_segment_manager()),
                *shm));
        if (!logger->EraseConnectionLogs(conn.transactions_.size() + 1)) {
          LOG(ERROR) << "Failed to undo log\n";
          logger->Log(abort_req_);
        }
        conn.transactions_.clear();
        shutdown = true;
        response = {error_response,
                    RESPTypeDeleter{shm->get_segment_manager()}};
      } else {
        conn.transactions_.push_back(req);
        logger->Log(req);
        auto string_response =
            shm->get_segment_manager()->template construct<RESPSimpleString>(
                bip::anonymous_instance)(ShmMakeShared(
                shm->get_segment_manager()->template construct<ShmString>(
                    bip::anonymous_instance)("QUEUED",
                                             shm->get_segment_manager()),
                *shm));
        response = {string_response,
                    RESPTypeDeleter{shm->get_segment_manager()}};
      }
    }
  } else {
    RESPType *response_ptr;
    std::tie(response_ptr, shutdown) =
        EmergencyServeImpl(req, conn, cache, logger, flow_control);
    response = {response_ptr, RESPTypeDeleter{shm->get_segment_manager()}};
  }

  return {Packet(boost::move(response)), shutdown};
}

std::pair<RESPType *, bool> Redis::EmergencyServeImpl(
    ShmSharedPtr<Packet> req, ConnectionInfo &conn, Cache *cache,
    Logger *logger, bool flow_control, const bool in_transaction) {
  return HandleUpdate(req, conn, cache, logger, flow_control, in_transaction,
                      true);
}

std::pair<RESPType *, bool> Redis::HandleUpdate(ShmSharedPtr<Packet> req,
                                                ConnectionInfo &conn,
                                                Cache *cache, Logger *logger,
                                                bool flow_control,
                                                const bool in_transaction,
                                                const bool in_emergency) {
  std::string_view opcode;
  try {
    opcode = req->GetOpcode();
  } catch (const std::exception &e) {
    const auto buffer = req->Serialize();
    LOG(ERROR) << "Unknown opcode: ";
    for (const auto &c : *buffer) LOG(ERROR) << c;
    LOG(ERROR) << std::endl;
  }
  CacheEntry entry(shm->get_segment_manager());
  if (flow_control)
    return {shm->get_segment_manager()->template construct<RESPError>(
                bip::anonymous_instance)(ShmMakeShared(
                shm->get_segment_manager()->template construct<ShmString>(
                    bip::anonymous_instance)("ERR flow control enabled",
                                             shm->get_segment_manager()),
                *shm)),
            false};
  if (opcode == "set") {
    const auto key = static_cast<RESPString *>(req->GetArg(0));
    const auto value = static_cast<RESPString *>(req->GetArg(1));
    CacheKey cache_key(*(key->value),
                       ShmAllocator<char>(shm->get_segment_manager()));
    entry.value = *(value->value);
    entry.type = CacheEntryType::STRING;
    if (cache->Set(cache_key, entry, in_transaction))
      if (in_emergency)
        return {
            shm->get_segment_manager()->template construct<RESPSimpleString>(
                bip::anonymous_instance)(ShmMakeShared(
                shm->get_segment_manager()->template construct<ShmString>(
                    bip::anonymous_instance)("OK", shm->get_segment_manager()),
                *shm)),
            false};
      else
        return {nullptr, false};
    return {shm->get_segment_manager()->template construct<RESPError>(
                bip::anonymous_instance)(ShmMakeShared(
                shm->get_segment_manager()->template construct<ShmString>(
                    bip::anonymous_instance)("ERR failed to set",
                                             shm->get_segment_manager()),
                *shm)),
            false};
  } else if (opcode == "get") {
    if (in_emergency) {
      const auto key = static_cast<RESPString *>(req->GetArg(0));
      CacheKey cache_key(*(key->value),
                         ShmAllocator<char>(shm->get_segment_manager()));
      if (cache->Get(cache_key, entry, in_transaction))
        return {shm->get_segment_manager()->template construct<RESPBulkString>(
                    bip::anonymous_instance)(ShmMakeShared(
                    shm->get_segment_manager()->template construct<ShmString>(
                        bip::anonymous_instance)(entry.value.c_str(),
                                                 shm->get_segment_manager()),
                    *shm)),
                false};
      else
        return {shm->get_segment_manager()->template construct<RESPBulkString>(
                    bip::anonymous_instance)(nullptr),
                false};
    } else
      return std::make_pair(nullptr, false);
    // } else if (opcode == "hmset") {
    //   const auto key = static_cast<RESPString *>(req->GetArg(0));
    //   auto map = std::make_shared<std::map<std::string, std::string>>();
    //   if (cache->Get(*(key->value), entry, in_transaction)) {
    //     map = entry.map_value
    //               ? entry.map_value
    //               : std::make_shared<std::map<std::string, std::string>>();
    //   }
    //   for (size_t i = 1; i < req->GetArgNum(); i += 2) {
    //     const auto field = static_cast<RESPString *>(req->GetArg(i));
    //     const auto value = static_cast<RESPString *>(req->GetArg(i + 1));
    //     (*map)[*field->value] = *value->value;
    //   }
    //   entry.type = CacheEntryType::MAP;
    //   entry.map_value = map;
    //   if (cache->Set(*(key->value), entry, in_transaction))
    //     if (in_emergency)
    //       return {new RESPSimpleString(std::make_shared<std::string>("OK")),
    //               false};
    //     else
    //       return {nullptr, false};
    //   return {new RESPError(std::make_shared<std::string>("ERR failed to
    //   set")),
    //           false};
    // } else if (opcode == "hgetall") {
    //   if (in_emergency) {
    //     const auto key = static_cast<RESPString *>(req->GetArg(0));
    //     if (cache->Get(*(key->value), entry, in_transaction)) {
    //       if (entry.map_value) {
    //         auto result = std::make_shared<
    //             std::map<std::unique_ptr<RESPType>,
    //             std::unique_ptr<RESPType>>>();
    //         for (const auto &pair : *entry.map_value) {
    //           result->insert(std::make_pair(
    //               std::make_unique<RESPSimpleString>(
    //                   std::make_shared<std::string>(pair.first)),
    //               std::make_unique<RESPSimpleString>(
    //                   std::make_shared<std::string>(pair.second))));
    //         }
    //         return {new RESPMap(result), false};
    //       }
    //     }
    //     return {new RESPMap(), false};
    //   }
    //   return std::make_pair(nullptr, false);
  } else if (opcode == "quit") {
    if (in_emergency)
      return {
          shm->get_segment_manager()->template construct<RESPSimpleString>(
              bip::anonymous_instance)(ShmMakeShared(
              shm->get_segment_manager()->template construct<ShmString>(
                  bip::anonymous_instance)("OK", shm->get_segment_manager()),
              *shm)),
          true};
    else
      return {nullptr, true};
  } else if (opcode == "ping") {
    if (in_emergency) {
      if (req->GetArgNum() == 0)
        return {
            shm->get_segment_manager()->template construct<RESPSimpleString>(
                bip::anonymous_instance)(ShmMakeShared(
                shm->get_segment_manager()->template construct<ShmString>(
                    bip::anonymous_instance)("PONG",
                                             shm->get_segment_manager()),
                *shm)),
            false};
      else if (req->GetArgNum() == 1) {
        const auto arg = static_cast<RESPString *>(req->GetArg(0));
        return {shm->get_segment_manager()->template construct<RESPBulkString>(
                    bip::anonymous_instance)(arg->value),
                false};
      }
    }
    return {shm->get_segment_manager()->template construct<RESPError>(
                bip::anonymous_instance)(ShmMakeShared(
                shm->get_segment_manager()->template construct<ShmString>(
                    bip::anonymous_instance)("ERR wrong number of arguments",
                                             shm->get_segment_manager()),
                *shm)),
            false};
  } else if (opcode == "multi") {
    if (in_emergency) {
      if (flow_control)
        return {shm->get_segment_manager()->template construct<RESPError>(
                    bip::anonymous_instance)(ShmMakeShared(
                    shm->get_segment_manager()->template construct<ShmString>(
                        bip::anonymous_instance)("ERR flow control enabled",
                                                 shm->get_segment_manager()),
                    *shm)),
                true};
      conn.is_in_transaction_ = true;
      logger->Log(req);
      return {
          shm->get_segment_manager()->template construct<RESPSimpleString>(
              bip::anonymous_instance)(ShmMakeShared(
              shm->get_segment_manager()->template construct<ShmString>(
                  bip::anonymous_instance)("OK", shm->get_segment_manager()),
              *shm)),
          false};
    }
  }
  // else if (opcode == "incr") {
  //   const auto key = static_cast<RESPString *>(req->GetArg(0));
  //   if (cache->Get(*(key->value), entry, in_transaction))
  //     *entry.value = std::to_string(std::stoll(*entry.value) + 1);
  //   else
  //     entry.value = std::make_shared<std::string>("1");
  //   entry.type = CacheEntryType::STRING;
  //   if (cache->Set(*(key->value), entry, in_transaction))
  //     return {new RESPInteger(entry.value), false};
  // } else if (opcode == "hset") {
  //   const auto key = static_cast<RESPString *>(req->GetArg(0));
  //   const auto field = static_cast<RESPString *>(req->GetArg(1));
  //   const auto value = static_cast<RESPString *>(req->GetArg(2));
  //   if (cache->Get(*(key->value), entry, in_transaction)) {
  //     auto map = entry.map_value
  //                    ? entry.map_value
  //                    : std::make_shared<std::map<std::string,
  //                    std::string>>();
  //     (*map)[*field->value] = *value->value;
  //     entry.map_value = map;
  //   } else {
  //     auto map = std::make_shared<std::map<std::string, std::string>>();
  //     (*map)[*field->value] = *value->value;
  //     entry.map_value = map;
  //   }
  //   entry.type = CacheEntryType::MAP;
  //   if (cache->Set(*(key->value), entry, in_transaction))
  //     return {new RESPSimpleString(std::make_shared<std::string>("OK")),
  //     false};
  //   return {new RESPError(std::make_shared<std::string>("ERR failed to
  //   set")),
  //           false};
  // } else if (opcode == "hget") {
  //   if (in_emergency) {
  //     const auto key = static_cast<RESPString *>(req->GetArg(0));
  //     const auto field = static_cast<RESPString *>(req->GetArg(1));
  //     if (cache->Get(*(key->value), entry, in_transaction)) {
  //       if (entry.map_value) {
  //         auto it = entry.map_value->find(*field->value);
  //         if (it != entry.map_value->end())
  //           return {
  //               new
  //               RESPBulkString(std::make_shared<std::string>(it->second)),
  //               false};
  //       }
  //     }
  //     return {new RESPBulkString(nullptr), false};
  //   }
  // } else if (opcode == "lpush") {
  //   const auto key = static_cast<RESPString *>(req->GetArg(0));
  //   auto list = std::make_shared<std::list<std::string>>();
  //   if (cache->Get(*(key->value), entry, in_transaction)) {
  //     if (entry.list_value)
  //       list = entry.list_value;
  //     for (size_t i = 1; i < req->GetArgNum(); i++) {
  //       const auto value = static_cast<RESPString *>(req->GetArg(i));
  //       list->push_front(*value->value);
  //     }
  //   } else {
  //     for (size_t i = 1; i < req->GetArgNum(); i++) {
  //       const auto value = static_cast<RESPString *>(req->GetArg(i));
  //       list->push_front(*value->value);
  //     }
  //   }
  //   entry.list_value = list;
  //   entry.type = CacheEntryType::LIST;
  //   if (cache->Set(*(key->value), entry, in_transaction))
  //     return {new RESPInteger(
  //                 std::make_shared<std::string>(std::to_string(list->size()))),
  //             false};
  // } else if (opcode == "rpush") {
  //   const auto key = static_cast<RESPString *>(req->GetArg(0));
  //   auto list = std::make_shared<std::list<std::string>>();
  //   if (cache->Get(*(key->value), entry, in_transaction)) {
  //     if (entry.list_value)
  //       list = entry.list_value;
  //     for (size_t i = 1; i < req->GetArgNum(); i++) {
  //       const auto value = static_cast<RESPString *>(req->GetArg(i));
  //       list->push_back(*value->value);
  //     }
  //   } else {
  //     for (size_t i = 1; i < req->GetArgNum(); i++) {
  //       const auto value = static_cast<RESPString *>(req->GetArg(i));
  //       list->push_back(*value->value);
  //     }
  //   }
  //   entry.list_value = list;
  //   entry.type = CacheEntryType::LIST;
  //   if (cache->Set(*(key->value), entry, in_transaction))
  //     return {new RESPInteger(
  //                 std::make_shared<std::string>(std::to_string(list->size()))),
  //             false};
  // } else if (opcode == "lpop") {
  //   const auto key = static_cast<RESPString *>(req->GetArg(0));
  //   auto list = std::make_shared<std::list<std::string>>();
  //   if (cache->Get(*(key->value), entry, in_transaction)) {
  //     if (entry.list_value != nullptr)
  //       list = entry.list_value;
  //     if (list->empty())
  //       return {new RESPBulkString(nullptr), false};
  //     auto value = list->front();
  //     list->pop_front();
  //     if (cache->Set(*(key->value), entry, in_transaction))
  //       return {new RESPBulkString(std::make_shared<std::string>(value)),
  //               false};
  //     return {new RESPError(std::make_shared<std::string>("ERR failed to
  //     set")),
  //             false};
  //   }
  //   return {new RESPBulkString(nullptr), false};
  // } else if (opcode == "rpop") {
  //   const auto key = static_cast<RESPString *>(req->GetArg(0));
  //   auto list = std::make_shared<std::list<std::string>>();
  //   if (cache->Get(*(key->value), entry, in_transaction)) {
  //     if (entry.list_value != nullptr)
  //       list = entry.list_value;
  //     if (list->empty())
  //       return {new RESPBulkString(nullptr), false};
  //     auto value = list->back();
  //     list->pop_back();
  //     if (cache->Set(*(key->value), entry, in_transaction))
  //       return {new RESPBulkString(std::make_shared<std::string>(value)),
  //               false};
  //     return {new RESPError(std::make_shared<std::string>("ERR failed to
  //     set")),
  //             false};
  //   }
  //   return {new RESPBulkString(nullptr), false};
  // } else if (opcode == "sadd") {
  //   const auto key = static_cast<RESPString *>(req->GetArg(0));
  //   auto set = std::make_shared<std::set<std::string>>();
  //   if (cache->Get(*(key->value), entry, in_transaction)) {
  //     if (entry.set_value != nullptr)
  //       set = entry.set_value;
  //     for (size_t i = 1; i < req->GetArgNum(); i++) {
  //       const auto value = static_cast<RESPString *>(req->GetArg(i));
  //       set->insert(*value->value);
  //     }
  //   } else {
  //     for (size_t i = 1; i < req->GetArgNum(); i++) {
  //       const auto value = static_cast<RESPString *>(req->GetArg(i));
  //       set->insert(*value->value);
  //     }
  //     entry.set_value = set;
  //   }
  //   entry.type = CacheEntryType::SET;
  //   if (cache->Set(*(key->value), entry, in_transaction))
  //     return {new RESPInteger(set->size()), false};
  //   return {new RESPError(std::make_shared<std::string>("ERR failed to
  //   set")),
  //           false};
  // } else if (opcode == "spop") {
  //   if (in_emergency) {
  //     const auto key = static_cast<RESPString *>(req->GetArg(0));
  //     auto set = std::make_shared<std::set<std::string>>();
  //     if (cache->Get(*(key->value), entry, in_transaction)) {
  //       if (entry.set_value != nullptr)
  //         set = entry.set_value;
  //       if (set->empty()) {
  //         return {new RESPBulkString(nullptr), false};
  //       }
  //       auto it = set->begin();
  //       std::advance(it, rand() % set->size());
  //       auto value = *it;
  //       set->erase(it);
  //       if (cache->Set(*(key->value), entry, in_transaction))
  //         return {new RESPBulkString(std::make_shared<std::string>(value)),
  //                 false};
  //       return {
  //           new RESPError(std::make_shared<std::string>("ERR failed to
  //           set")), false};
  //     }
  //   }
  //   // TODO: update cache according to response of spop in normal mode
  //   return {new RESPBulkString(nullptr), false};
  // } else if (opcode == "zadd") {
  //   const auto key = static_cast<RESPString *>(req->GetArg(0));
  //   if (cache->Get(*(key->value), entry, in_transaction)) {
  //     if (entry.sorted_set_value == nullptr)
  //       entry.sorted_set_value =
  //           std::make_shared<std::map<double, std::string>>();
  //     for (size_t i = 1; i < req->GetArgNum(); i += 2) {
  //       const auto score = static_cast<RESPString *>(req->GetArg(i));
  //       const auto member = static_cast<RESPString *>(req->GetArg(i + 1));
  //       if (member == nullptr || score == nullptr)
  //         return {new RESPError(std::make_shared<std::string>(
  //                     "ERR wrong type of arguments")),
  //                 false};
  //       entry.sorted_set_value->insert(
  //           std::make_pair(std::stod(*score->value), *member->value));
  //     }
  //   } else {
  //     entry.type = CacheEntryType::ZSET;
  //     entry.sorted_set_value =
  //         std::make_shared<std::map<double, std::string>>();
  //     for (size_t i = 1; i < req->GetArgNum(); i += 2) {
  //       const auto score = static_cast<RESPString *>(req->GetArg(i));
  //       const auto member = static_cast<RESPString *>(req->GetArg(i + 1));
  //       entry.sorted_set_value->insert(
  //           std::make_pair(std::stod(*score->value), *member->value));
  //     }
  //   }
  //   if (cache->Set(*(key->value), entry, in_transaction))
  //     return {new RESPInteger(entry.sorted_set_value->size()), false};
  //   return {new RESPError(std::make_shared<std::string>("ERR failed to
  //   set")),
  //           false};
  // } else if (opcode == "zpopmin") {
  //   const auto key = static_cast<RESPString *>(req->GetArg(0));
  //   auto zset = std::make_shared<std::map<double, std::string>>();
  //   if (cache->Get(*(key->value), entry, in_transaction)) {
  //     if (entry.sorted_set_value != nullptr)
  //       zset = entry.sorted_set_value;
  //     if (zset->empty())
  //       return {new RESPBulkString(nullptr), false};
  //     auto it = zset->begin();
  //     auto value = it->second;
  //     zset->erase(it);
  //     if (cache->Set(*(key->value), entry, in_transaction))
  //       return {new RESPBulkString(std::make_shared<std::string>(value)),
  //               false};
  //     return {new RESPError(std::make_shared<std::string>("ERR failed to
  //     pop")),
  //             false};
  //   }
  //   return {new RESPBulkString(nullptr), false};
  // }
  LOG(ERROR) << "Unknown opcode1: " << opcode << std::endl;
  if (in_emergency)
    return {shm->get_segment_manager()->template construct<RESPError>(
                bip::anonymous_instance)(ShmMakeShared(
                shm->get_segment_manager()->template construct<ShmString>(
                    bip::anonymous_instance)("ERR unknown opcode",
                                             shm->get_segment_manager()),
                *shm)),
            false};
  return {nullptr, false};
}

#define LRU_BITS 24

struct robj {
  unsigned type : 4;
  unsigned encoding : 4;
  unsigned lru : LRU_BITS; /* LRU time (relative to global lru_clock) or
                            * LFU data (least significant 8 bits frequency
                            * and most significant 16 bits access time). */
  int refcount;
  void *ptr;
};

enum class EmbeddedRequestType {
  kSet,
  kHset,
};

typedef struct {
  EmbeddedRequestType type;
  int argc;
  robj **argv;
  int *argv_len;
} EmbeddedRequest;

int Redis::EmbeddedNormalUpdate(void *request, ConnectionInfo &conn,
                                Cache *cache) {
  auto embedded_request = static_cast<EmbeddedRequest *>(request);
  if (embedded_request->type == EmbeddedRequestType::kSet) {
    char *key = static_cast<char *>(embedded_request->argv[1]->ptr);
    char *value = static_cast<char *>(embedded_request->argv[2]->ptr);
    ShmString shm_key(key, shm->get_segment_manager());
    ShmString shm_value(value, shm->get_segment_manager());

    CacheKey cache_key(shm_key, ShmAllocator<char>(shm->get_segment_manager()));
    CacheEntry cache_entry(shm->get_segment_manager());
    cache_entry.value = shm_value;
    cache_entry.type = CacheEntryType::STRING;

    if (!cache->Set(cache_key, cache_entry)) {
      LOG(ERROR) << "Failed to set key: " << cache_key << std::endl;
    }
    return 0;
    // } else if (embedded_request->type == EmbeddedRequestType::kHset) {
    //   char *key = static_cast<char *>(embedded_request->argv[1]->ptr);

    //   CacheKey cache_key(shm_key,
    //                       ShmAllocator<char>(shm->get_segment_manager()));
    //   CacheEntry cache_entry(shm->get_segment_manager());
    //   if (!cache->Get(cache_key, cache_entry)) {
    //     auto map =

    //   for (int i = 2; i < embedded_request->argc; i += 2) {
    //     char *field = static_cast<char *>(embedded_request->argv[i]->ptr);
    //     char *value = static_cast<char *>(embedded_request->argv[i +
    //     1]->ptr); ShmString shm_key(field, shm->get_segment_manager());
    //     ShmString shm_value(value, shm->get_segment_manager());
    //     map->insert(std::make_pair(shm_key, shm_value));
    //   }
  }

  LOG(ERROR) << "Unknown request type: " << int(embedded_request->type)
             << std::endl;
  return -1;
}