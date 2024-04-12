#include "service.hpp"

#include "connection.hpp"

LevelDBService::LevelDBService(const size_t &max_item_count,
                               std::string &backend_addr,
                               std::string &backend_port)
    : cache_(max_item_count),
      backend_addr_(backend_addr),
      backend_port_(backend_port) {}

// TODO: support multi exec
bool LevelDBService::Filter(const std::shared_ptr<Packet> &p) const {
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
    p->connection->is_in_transaction_ = true;
    return false;
  } else if (opcode == "exec") {
    return true;
  }
  if (p->connection->is_in_transaction_) {
    p->connection->transactions_.push_back(p);
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

void LevelDBService::NormalUpdate(const std::shared_ptr<Packet> &p) {
  if (p->connection->is_in_transaction_) {
    {
      auto cache_lock = cache_.TransactionLock();
      for (const auto &c : p->connection->transactions_) {
        NormalUpdateImpl(c, true);
      }
    }

    p->connection->is_in_transaction_ = false;
    p->connection->transactions_.clear();
  } else {
    NormalUpdateImpl(p);
  }
}

void LevelDBService::NormalUpdateImpl(const std::shared_ptr<Packet> &p,
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
    cache_.Set(*(key->value), entry, in_transaction);
  } else if (opcode != "get" && opcode != "ping") {
    std::cerr << "Unknow opcode: " << opcode << std::endl;
  }
}

void LevelDBService::NormalForwardAndProxyBack(
    std::shared_ptr<Packet> p, Connection *conn_ptr,
    volatile evutil_socket_t &server_fd) {
  if (server_fd <= 0) {
    if (!conn_ptr->ConnectBackend()) {
      std::cerr << "Fall back to EmergencyServe\n";
      EmergencyServe(std::move(p), conn_ptr);
      return;
    }
  }
  // TODO: do we really need to have this copy?
  std::vector<uint8_t> buffer;
  p->AppendToBuffer(buffer);

  write(server_fd, buffer.data(), buffer.size());
}

void LevelDBService::EmergencyServe(std::shared_ptr<Packet> p,
                                    Connection *conn_ptr) {
  RESPType *response = nullptr;
  if (conn_ptr->is_in_transaction_) {
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
        auto cache_lock = cache_.TransactionLock();
        for (const auto &c : conn_ptr->transactions_) {
          response_array->value->emplace_back(
              EmergencyServeImpl(c, conn_ptr, true));
        }
      }

      {
        auto logger_lock = logger_.TransactionLock();
        for (const auto &c : conn_ptr->transactions_) {
          logger_.Log(LogEntry{c}, true);
        }
        logger_.Log(LogEntry{p}, true);
      }

      conn_ptr->is_in_transaction_ = false;
      conn_ptr->transactions_.clear();
      response = response_array;
    } else {
      conn_ptr->transactions_.push_back(p);
      response = new RESPSimpleString(std::make_shared<std::string>("QUEUED"));
    }
  } else {
    logger_.Log(LogEntry{p});
    response = EmergencyServeImpl(std::move(p), conn_ptr);
  }

  std::vector<uint8_t> buffer;
  response->AppendToBuffer(buffer);
  conn_ptr->Write(std::make_unique<std::vector<uint8_t>>(std::move(buffer)));
  delete response;
}

RESPType *LevelDBService::EmergencyServeImpl(std::shared_ptr<Packet> p,
                                             Connection *conn_ptr,
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
    if (cache_.Set(*(key->value), entry, in_transaction))
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
    if (cache_.Get(*(key->value), entry, in_transaction)) {
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
    conn_ptr->is_in_transaction_ = true;
    return new RESPSimpleString(std::make_shared<std::string>("OK"));
  }

  std::cerr << "Unknow opcode: " << opcode << std::endl;
  return new RESPError(std::make_shared<std::string>("ERR unknow command"));
}

void LevelDBService::Replay() {
  int backend_fd, tries = 0;
  while ((backend_fd = Connection::TryConnectBackend(backend_addr_,
                                                     backend_port_)) == -1) {
    if (tries++ > 100) {
      std::cerr << "Replay failed to connect to backend\n";
      return;
    }
  }
  std::cerr << "Replay connected to backend in " << tries << " tries\n";
  size_t cnt = 0;
  LogEntry entry;
  while (logger_.Pop(entry)) {
    cnt++;
    std::vector<uint8_t> buffer;
    entry.value->AppendToBuffer(buffer);
    write(backend_fd, buffer.data(), buffer.size());  // TODO: less writes
  }
  // TODO: in-flight requests after this?
  close(backend_fd);
  std::cerr << "Replay " << cnt << " items\n";
}

void LevelDBService::BackendHandler(evutil_socket_t fd, short which,
                                    void *arg_conn) {
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