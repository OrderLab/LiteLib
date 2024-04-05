#include "service.hpp"

#include "connection.hpp"

LevelDBService::LevelDBService(const size_t &max_item_count,
                               std::string &backend_addr,
                               std::string &backend_port)
    : cache_(max_item_count),
      backend_addr_(backend_addr),
      backend_port_(backend_port) {}

// TODO: support multi exec
bool LevelDBService::Filter(const std::unique_ptr<Packet> &p) const {
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
  if (opcode == "set") {
    return false;
  }
  return false;
}

void LevelDBService::NormalUpdate(const std::unique_ptr<Packet> &p) {
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
    const auto value = dynamic_cast<RESPString *>(p->GetArg(0).get());
    if (value == nullptr) {
      std::cerr << "Invalid argument for set\n";
      return;
    }
    entry.value = value->value;
    cache_.Set(*(key->value), entry);
  } else {
    std::cerr << "Unknow opcode: " << opcode << std::endl;
  }
}

void LevelDBService::NormalForwardAndProxyBack(
    std::unique_ptr<Packet> p, Connection *conn_ptr,
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

void LevelDBService::EmergencyServe(std::unique_ptr<Packet> p,
                                    Connection *conn_ptr) {
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
  RESPType *response = nullptr;
  if (opcode == "set") {
    if (p->GetArgNum() != 2) {
      std::cerr << "Invalid number of arguments for set" << std::endl;
      response = new RESPError(
          std::make_shared<std::string>("ERR wrong number of arguments"));
      goto send_response;
    }
    const auto key = dynamic_cast<RESPString *>(p->GetArg(0).get());
    if (key == nullptr) {
      std::cerr << "Invalid argument for set\n";
      response = new RESPError(
          std::make_shared<std::string>("ERR wrong type of arguments"));
      goto send_response;
    }
    const auto value = dynamic_cast<RESPString *>(p->GetArg(0).get());
    if (value == nullptr) {
      std::cerr << "Invalid argument for set\n";
      response = new RESPError(
          std::make_shared<std::string>("ERR wrong type of arguments"));
      goto send_response;
    }
    entry.value = value->value;
    if (cache_.Set(*(key->value), entry))
      response = new RESPSimpleString(std::make_shared<std::string>("OK"));
  } else if (opcode == "get") {
    if (p->GetArgNum() != 1) {
      std::cerr << "Invalid number of arguments for get" << std::endl;
      response = new RESPError(
          std::make_shared<std::string>("ERR wrong number of arguments"));
      goto send_response;
    }
    const auto key = dynamic_cast<RESPString *>(p->GetArg(0).get());
    if (key == nullptr) {
      std::cerr << "Invalid argument for get\n";
      response = new RESPError(
          std::make_shared<std::string>("ERR wrong type of arguments"));
      goto send_response;
    }
    if (cache_.Get(*(key->value), entry)) {
      response = new RESPBulkString(entry.value);
    } else {
      response = new RESPBulkString(nullptr);
    }
  } else if (opcode == "ping") {
    if (p->GetArgNum() == 0) {
      response = new RESPSimpleString(std::make_shared<std::string>("PONG"));
    } else if (p->GetArgNum() == 1) {
      const auto arg = dynamic_cast<RESPString *>(p->GetArg(0).get());
      if (arg == nullptr) {
        std::cerr << "Invalid argument for ping\n";
        response = new RESPError(
            std::make_shared<std::string>("ERR wrong type of arguments"));
        goto send_response;
      }
      response = new RESPBulkString(arg->value);
    } else {
      std::cerr << "Invalid number of arguments for ping" << std::endl;
      response = new RESPError(
          std::make_shared<std::string>("ERR wrong number of arguments"));
      goto send_response;
    }
  } else {
    std::cerr << "Unknow opcode: " << opcode << std::endl;
    response =
        new RESPError(std::make_shared<std::string>("ERR unknow command"));
    goto send_response;
  }

send_response:
  std::vector<uint8_t> buffer;
  response->AppendToBuffer(buffer);
  conn_ptr->Write(std::make_unique<std::vector<uint8_t>>(std::move(buffer)));
}

void LevelDBService::Replay() {
  // TODO
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
  buffer->resize(bytes_transferred);
  conn->Write(std::move(buffer));
}