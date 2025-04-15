#include "service.hpp"

std::shared_ptr<Packet> LevelDB::abort_req_ = nullptr;

LevelDB::LevelDB() {
  if (abort_req_) return;
  abort_req_ = std::make_shared<Packet>();
  auto discard_comm = std::make_unique<RESPArray>();
  discard_comm->value.emplace_back(std::make_unique<RESPBulkString>(
      std::make_shared<std::string>("DISCARD")));
  abort_req_->command = std::move(discard_comm);
}

std::pair<std::vector<std::shared_ptr<Packet>>, bool> LevelDB::Match(
    const std::shared_ptr<Packet> &resp, ConnectionInfo &conn,
    lite::ThreadSafeQueue<std::pair<std::shared_ptr<Packet>, bool>>
        &pending_requests) const {
  // std::cout << "Pending requests size: " << pending_requests.size() << std::endl;
  if (pending_requests.size() != 1) {
    LOG(FATAL) << "Pending requests is not 1, it is " << pending_requests.size() << "\n";
    exit(1);
  }
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
  if (*opcode == "set" || *opcode == "get" || *opcode == "getset") {
    return std::make_pair(std::vector<std::shared_ptr<Packet>>{req},
                          is_not_replay);
  } else if (*opcode == "ping") {
    return std::make_pair(std::vector<std::shared_ptr<Packet>>{req},
                          is_not_replay);
  }
  LOG(ERROR) << "Unknow opcode: " << *opcode << std::endl;
  return std::make_pair(std::vector<std::shared_ptr<Packet>>(), is_not_replay);
}

void LevelDB::NormalUpdate(const std::shared_ptr<Packet> &resp,
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
      for (size_t i = 0; i < len; ++i) {
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

void LevelDB::NormalUpdateImpl(const std::shared_ptr<Packet> &req, Cache *cache,
                               const bool in_transaction) {
  std::string_view opcode;
  try {
    opcode = req->GetOpcode();
  } catch (const std::exception &e) {
    const auto buffer = req->Serialize();
    LOG(ERROR) << "Unknow opcode: ";
    for (const auto &c : *buffer) LOG(ERROR) << c;
    LOG(ERROR) << std::endl;
  }
  CacheEntry entry;
  if (opcode == "set" || opcode == "getset") {
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
    entry.value = value->value;
    cache->Set(*(key->value), entry, in_transaction);
    // std::cout<<*(key->value)<<" "<<entry.value->at(0)<<std::endl;
  } else if (opcode != "get" &&
             opcode != "ping") {  // TODO: update states using get
    LOG(ERROR) << "Unknow opcode: " << opcode << std::endl;
  }
}

void LevelDB::HandleReplayResponse(
    const std::shared_ptr<Packet> &resp,
    std::vector<std::shared_ptr<Packet>> requests, ConnectionInfo &conn,
    Cache *cache) {
  auto error_msg = dynamic_cast<RESPError *>(resp->command.get());
  if (error_msg) {
    LOG(ERROR) << "Received error msg from full during replay: "
               << *error_msg->value << std::endl;
    exit(1);  // TODO: handle error
  }
  return;
}

std::pair<Packet, bool> LevelDB::EmergencyServe(std::shared_ptr<Packet> req,
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

std::pair<RESPType *, bool> LevelDB::EmergencyServeImpl(
    std::shared_ptr<Packet> req, ConnectionInfo &conn, Cache *cache,
    Logger *logger, bool flow_control, const bool in_transaction) {
  std::string_view opcode;
  try {
    opcode = req->GetOpcode();
  } catch (const std::exception &e) {
    const auto buffer = req->Serialize();
    LOG(ERROR) << "Unknow opcode: ";
    for (const auto &c : *buffer) LOG(ERROR) << c;
    LOG(ERROR) << std::endl;
  }
  CacheEntry entry;
  if (opcode == "set" || opcode == "getset") {
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
    CacheEntry old_entry;
    bool found = false;
    if (opcode == "getset") {
      found = cache->Get(*(key->value), old_entry, in_transaction);
    }
    if (cache->Set(*(key->value), entry, in_transaction)) {
      if (opcode == "getset") {
        if (found)
          return {new RESPBulkString(old_entry.value), false};
        else
          return {new RESPBulkString(nullptr), false};
      }
      return {new RESPSimpleString(std::make_shared<std::string>("OK")), false};
    }
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
  }

  LOG(FATAL) << "unknown opcode: " << opcode << std::endl;
  return {new RESPError(std::make_shared<std::string>("ERR unknown command")),
          false};
}
