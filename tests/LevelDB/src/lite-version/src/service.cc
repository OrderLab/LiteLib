#include "service.hpp"

#include "connection.hpp"

LevelDB::LevelDB(std::string &backend_addr, std::string &backend_port)
    : backend_addr_(backend_addr), backend_port_(backend_port) {}

// TODO: support multi exec
bool LevelDB::Filter(const std::shared_ptr<Packet> &p, Connection &conn) const {
  std::string_view opcode;
  try {
    opcode = p->GetOpcode();
  } catch (const std::exception &e) {
    std::vector<std::uint8_t> buffer;
    p->AppendToBuffer(buffer);
    std::cerr << "Unknow opcode: ";
    for (const auto &c : buffer) std::cerr << c;
    std::cerr << std::endl;
    return false;
  }
  if (opcode == "multi") {
    conn.is_in_transaction_ = true;
    return false;
  } else if (opcode == "exec") {
    return true;
  }
  if (conn.is_in_transaction_) {
    conn.transactions_.push_back(p);
    return false;
  }
  if (opcode == "set") {
    return true;
  } else if (opcode == "get" || opcode == "ping") {
    return false;
  }
  std::cerr << "Unknow opcode: " << opcode << std::endl;
  return false;
}

void LevelDB::NormalUpdate(const std::shared_ptr<Packet> &p, Connection &conn,
                           Cache &cache) {
  if (conn.is_in_transaction_) {
    {
      auto cache_lock = cache.TransactionLock();
      for (const auto &c : conn.transactions_) {
        NormalUpdateImpl(c, cache, true);
      }
    }

    conn.is_in_transaction_ = false;
    conn.transactions_.clear();
  } else {
    NormalUpdateImpl(p, cache);
  }
}

void LevelDB::NormalUpdateImpl(const std::shared_ptr<Packet> &p, Cache &cache,
                               const bool in_transaction) {
  std::string_view opcode;
  try {
    opcode = p->GetOpcode();
  } catch (const std::exception &e) {
    std::vector<std::uint8_t> buffer;
    p->AppendToBuffer(buffer);
    std::cerr << "Unknow opcode: ";
    for (const auto &c : buffer) std::cerr << c;
    std::cerr << std::endl;
  }
  CacheEntry entry;
  if (opcode == "set") {
    if (p->GetArgNum() != 2) {
      std::cerr << "Invalid number of arguments for set\n";
      return;
    }
    const auto key = dynamic_cast<RESPString *>(p->GetArg(0).get());
    if (key == nullptr) {
      std::cerr << "Invalid argument for set\n";
      return;
    }
    const auto value = dynamic_cast<RESPString *>(p->GetArg(1).get());
    if (value == nullptr) {
      std::cerr << "Invalid argument for set\n";
      return;
    }
    entry.value = value->value;
    cache.Set(*(key->value), entry, in_transaction);
  } else if (opcode != "get" && opcode != "ping") {
    std::cerr << "Unknow opcode: " << opcode << std::endl;
  }
}

void LevelDB::EmergencyServe(std::shared_ptr<Packet> p, Connection &conn,
                             Cache &cache, Logger &logger) {
  RESPType *response = nullptr;
  if (conn.is_in_transaction_) {
    std::string_view opcode;
    try {
      opcode = p->GetOpcode();
    } catch (const std::exception &e) {
      std::vector<std::uint8_t> buffer;
      p->AppendToBuffer(buffer);
      std::cerr << "Unknow opcode: ";
      for (const auto &c : buffer) std::cerr << c;
      std::cerr << std::endl;
    }
    if (opcode == "exec") {
      auto response_array = new RESPArray;

      {
        auto cache_lock = cache.TransactionLock();
        for (const auto &c : conn.transactions_) {
          response_array->value->emplace_back(
              EmergencyServeImpl(c, conn, cache, logger, true));
        }
      }

      {
        auto logger_lock = logger.TransactionLock();
        for (const auto &c : conn.transactions_) {
          logger.Log(LogEntry{c}, true);
        }
        logger.Log(LogEntry{p}, true);
      }

      conn.is_in_transaction_ = false;
      conn.transactions_.clear();
      response = response_array;
    } else {
      conn.transactions_.push_back(p);
      response = new RESPSimpleString(std::make_shared<std::string>("QUEUED"));
    }
  } else {
    logger.Log(LogEntry{p});
    response = EmergencyServeImpl(std::move(p), conn, cache, logger);
  }

  std::vector<uint8_t> buffer;
  response->AppendToBuffer(buffer);
  conn.Write(std::make_unique<std::vector<uint8_t>>(std::move(buffer)));
  delete response;
}

RESPType *LevelDB::EmergencyServeImpl(std::shared_ptr<Packet> p,
                                      Connection &conn, Cache &cache,
                                      Logger &logger,
                                      const bool in_transaction) {
  std::string_view opcode;
  try {
    opcode = p->GetOpcode();
  } catch (const std::exception &e) {
    std::vector<std::uint8_t> buffer;
    p->AppendToBuffer(buffer);
    std::cerr << "Unknow opcode: ";
    for (const auto &c : buffer) std::cerr << c;
    std::cerr << std::endl;
  }
  CacheEntry entry;
  if (opcode == "set") {
    if (p->GetArgNum() != 2) {
      std::cerr << "Invalid number of arguments for set" << std::endl;
      return new RESPError(
          std::make_shared<std::string>("ERR wrong number of arguments"));
    }
    const auto key = dynamic_cast<RESPString *>(p->GetArg(0).get());
    if (key == nullptr) {
      std::cerr << "Invalid argument for set\n";
      return new RESPError(
          std::make_shared<std::string>("ERR wrong type of arguments"));
    }
    const auto value = dynamic_cast<RESPString *>(p->GetArg(1).get());
    if (value == nullptr) {
      std::cerr << "Invalid argument for set\n";
      return new RESPError(
          std::make_shared<std::string>("ERR wrong type of arguments"));
    }
    entry.value = value->value;
    if (cache.Set(*(key->value), entry, in_transaction))
      return new RESPSimpleString(std::make_shared<std::string>("OK"));
  } else if (opcode == "get") {
    if (p->GetArgNum() != 1) {
      std::cerr << "Invalid number of arguments for get" << std::endl;
      return new RESPError(
          std::make_shared<std::string>("ERR wrong number of arguments"));
    }
    const auto key = dynamic_cast<RESPString *>(p->GetArg(0).get());
    if (key == nullptr) {
      std::cerr << "Invalid argument for get\n";
      return new RESPError(
          std::make_shared<std::string>("ERR wrong type of arguments"));
    }
    if (cache.Get(*(key->value), entry, in_transaction)) {
      return new RESPBulkString(entry.value);
    } else {
      return new RESPBulkString(nullptr);
    }
  } else if (opcode == "ping") {
    if (p->GetArgNum() == 0) {
      return new RESPSimpleString(std::make_shared<std::string>("PONG"));
    } else if (p->GetArgNum() == 1) {
      const auto arg = dynamic_cast<RESPString *>(p->GetArg(0).get());
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
    return new RESPSimpleString(std::make_shared<std::string>("OK"));
  }

  std::cerr << "Unknow opcode: " << opcode << std::endl;
  return new RESPError(std::make_shared<std::string>("ERR unknow command"));
}

void LevelDB::Replay(Logger &logger) {}

void LevelDB::BackendHandler(evutil_socket_t fd, short which, void *arg_conn) {
  auto conn = static_cast<Connection *>(arg_conn);

  std::unique_ptr<std::vector<uint8_t>> buffer =
      std::make_unique<std::vector<uint8_t>>(16384);
  const ssize_t bytes_transferred =
      read(conn->backend_fd_, buffer->data(), 16384);
  if (bytes_transferred <= 0) {
    // TODO: maybe we can switch to emergency mode automatically here
    perror("read from backend");
    delete conn;
    return;
  }
  conn->Write(std::move(buffer), bytes_transferred);
}