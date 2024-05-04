#include "service.hpp"

std::pair<std::vector<std::shared_ptr<Packet>>, bool> LevelDB::Match(
    const std::shared_ptr<Packet> &resp, ConnectionInfo &conn,
    std::deque<std::pair<std::shared_ptr<Packet>, bool>> &pending_requests)
    const {
  auto [req, is_not_replay] = pending_requests.front();
  pending_requests.pop_front();
  RESPArray *command = dynamic_cast<RESPArray *>(req->command.get());
  auto opcode_resp = dynamic_cast<RESPBulkString *>(command->value[0].get());
  if (opcode_resp == nullptr) {
    std::cerr << "Invalid request\n";
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
  }
  std::cerr << "Unknow opcode: " << *opcode << std::endl;
  return std::make_pair(std::vector<std::shared_ptr<Packet>>(), is_not_replay);
}

void LevelDB::NormalUpdate(const std::shared_ptr<Packet> &resp,
                           std::vector<std::shared_ptr<Packet>> requests,
                           ConnectionInfo &conn, Cache *cache) {
  if (requests.empty()) return;
  if (conn.is_in_transaction_) {
    RESPArray *responses_resp = dynamic_cast<RESPArray *>(resp->command.get());
    if (responses_resp == nullptr) {
      std::cerr << "Invalid response for EXEC:";
      auto response_buffer = resp->Serialize();
      for (const auto &c : *response_buffer) std::cerr << c;
      std::cerr << std::endl;
      return;
    }
    auto &responses = responses_resp->value;
    if (conn.transactions_.size() != responses.size()) {
      std::cerr << "Invalid number of responses: trans "
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
    std::cerr << "Unknow opcode: ";
    for (const auto &c : *buffer) std::cerr << c;
    std::cerr << std::endl;
  }
  CacheEntry entry;
  if (opcode == "set") {
    if (req->GetArgNum() != 2) {
      std::cerr << "Invalid number of arguments for set\n";
      return;
    }
    const auto key = dynamic_cast<RESPString *>(req->GetArg(0));
    if (key == nullptr) {
      std::cerr << "Invalid argument for set\n";
      return;
    }
    const auto value = dynamic_cast<RESPString *>(req->GetArg(1));
    if (value == nullptr) {
      std::cerr << "Invalid argument for set\n";
      return;
    }
    entry.value = value->value;
    cache->Set(*(key->value), entry, in_transaction);
  } else if (opcode != "get" &&
             opcode != "ping") {  // TODO: update states using get
    std::cerr << "Unknow opcode: " << opcode << std::endl;
  }
}

void LevelDB::HandleReplayResponse(
    const std::shared_ptr<Packet> &resp,
    std::vector<std::shared_ptr<Packet>> requests, ConnectionInfo &conn,
    Cache *cache) {
  auto error_msg = dynamic_cast<RESPError *>(resp->command.get());
  if (error_msg) {
    std::cerr << "Received error msg from full during replay: "
              << error_msg->value << std::endl;
    exit(1);  // TODO: handle error
  }
  return;
}

Packet LevelDB::EmergencyServe(std::shared_ptr<Packet> req,
                               ConnectionInfo &conn, Cache *cache,
                               Logger *logger) {
  RESPArray *command = dynamic_cast<RESPArray *>(req->command.get());
  auto opcode_resp = dynamic_cast<RESPBulkString *>(command->value[0].get());
  if (opcode_resp == nullptr) {
    std::cerr << "Invalid request\n";
    return {};
  }
  auto &opcode = opcode_resp->value;
  std::transform(opcode->begin(), opcode->end(), opcode->begin(),
                 [](unsigned char c) { return std::tolower(c); });

  RESPType *response = nullptr;
  if (conn.is_in_transaction_) {
    if (*opcode == "exec") {
      if (!logger->EraseConnectionLogs(conn.transactions_.size() + 1)) {
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
        for (const auto &c : conn.transactions_) {
          response_array->value.emplace_back(
              EmergencyServeImpl(c, conn, cache, logger, true));
        }
      }

      conn.is_in_transaction_ = false;
      conn.transactions_.clear();
      response = response_array;
    } else {
      conn.transactions_.push_back(req);
      logger->Log(req);
      response = new RESPSimpleString(std::make_shared<std::string>("QUEUED"));
    }
  } else {
    response = EmergencyServeImpl(req, conn, cache, logger);
  }
  return Packet(std::unique_ptr<RESPType>(response));
}

RESPType *LevelDB::EmergencyServeImpl(std::shared_ptr<Packet> req,
                                      ConnectionInfo &conn, Cache *cache,
                                      Logger *logger,
                                      const bool in_transaction) {
  std::string_view opcode;
  try {
    opcode = req->GetOpcode();
  } catch (const std::exception &e) {
    const auto buffer = req->Serialize();
    std::cerr << "Unknow opcode: ";
    for (const auto &c : *buffer) std::cerr << c;
    std::cerr << std::endl;
  }
  CacheEntry entry;
  if (opcode == "set") {
    if (req->GetArgNum() != 2) {
      std::cerr << "Invalid number of arguments for set" << std::endl;
      return new RESPError(
          std::make_shared<std::string>("ERR wrong number of arguments"));
    }
    const auto key = dynamic_cast<RESPString *>(req->GetArg(0));
    if (key == nullptr) {
      std::cerr << "Invalid argument for set\n";
      return new RESPError(
          std::make_shared<std::string>("ERR wrong type of arguments"));
    }
    const auto value = dynamic_cast<RESPString *>(req->GetArg(1));
    if (value == nullptr) {
      std::cerr << "Invalid argument for set\n";
      return new RESPError(
          std::make_shared<std::string>("ERR wrong type of arguments"));
    }
    entry.value = value->value;
    if (cache->Set(*(key->value), entry, in_transaction))
      return new RESPSimpleString(std::make_shared<std::string>("OK"));
  } else if (opcode == "get") {
    if (req->GetArgNum() != 1) {
      std::cerr << "Invalid number of arguments for get" << std::endl;
      return new RESPError(
          std::make_shared<std::string>("ERR wrong number of arguments"));
    }
    const auto key = dynamic_cast<RESPString *>(req->GetArg(0));
    if (key == nullptr) {
      std::cerr << "Invalid argument for get\n";
      return new RESPError(
          std::make_shared<std::string>("ERR wrong type of arguments"));
    }
    if (cache->Get(*(key->value), entry, in_transaction)) {
      return new RESPBulkString(entry.value);
    } else {
      return new RESPBulkString(nullptr);
    }
  } else if (opcode == "ping") {
    if (req->GetArgNum() == 0) {
      return new RESPSimpleString(std::make_shared<std::string>("PONG"));
    } else if (req->GetArgNum() == 1) {
      const auto arg = dynamic_cast<RESPString *>(req->GetArg(0));
      if (arg == nullptr) {
        std::cerr << "Invalid argument for ping\n";
        return new RESPError(
            std::make_shared<std::string>("ERR wrong type of arguments"));
      }
      return new RESPBulkString(arg->value);
    } else {
      std::cerr << "Invalid number of arguments for ping" << std::endl;
      return new RESPError(
          std::make_shared<std::string>("ERR wrong number of arguments"));
    }
  } else if (opcode == "multi") {
    conn.is_in_transaction_ = true;
    logger->Log(req);
    return new RESPSimpleString(std::make_shared<std::string>("OK"));
  }

  std::cerr << "Unknow opcode: " << opcode << std::endl;
  return new RESPError(std::make_shared<std::string>("ERR unknow command"));
}
