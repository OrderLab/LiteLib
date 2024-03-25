#include "service.hpp"

#include "connection.hpp"

MemcachedService::MemcachedService(const size_t &max_item_count,
                                   std::string &backend_addr,
                                   std::string &backend_port)
    : cache_(max_item_count),
      backend_addr_(backend_addr),
      backend_port_(backend_port) {}

bool MemcachedService::Filter(const std::unique_ptr<Packet> &p) const {
  const auto OpCodeOption = p->GetOpcode();
  if (!OpCodeOption.has_value()) {
    std::cerr << "Unknow opcode: " << (*p->buffer)[1] << std::endl;
    return false;
  }
  const auto OpCode = OpCodeOption.value();
  if (OpCode == Header::Opcode::kGet || OpCode == Header::Opcode::kGetK ||
      OpCode == Header::Opcode::kGetKQ || OpCode == Header::Opcode::kNoOp ||
      OpCode == Header::Opcode::kStat || OpCode == Header::Opcode::kVersion) {
    return false;
  }
  return true;
}

void MemcachedService::NormalUpdate(const std::unique_ptr<Packet> &p) {
  const auto opcode = p->GetOpcode().value();
  const auto packet = ParsedPacket(*p);
  if (packet.header.magic != 0x80) {
    std::cerr << "Unsupported Protocol Version:\n" << packet << std::endl;
    // TODO: error handling
    exit(1);
  }
  packet.extra->resize(4);
  CacheEntry entry{
      .value = packet.value, .flags = packet.extra, .CAS = packet.header.CAS};
  switch (opcode) {
    case Header::Opcode::kSet:
    case Header::Opcode::kAdd:
      cache_.Add(*packet.key, entry);
      break;
    case Header::Opcode::kReplace:
      cache_.Replace(*packet.key, entry);
      break;
    case Header::Opcode::kQuit:
      break;
    default:  // TODO: support CAS, Expiration, error and other operations
      std::cerr << "Unknown OpCode: " << magic_enum::enum_name(opcode)
                << std::endl;
  }
}

void MemcachedService::NormalForwardAndProxyBack(
    std::unique_ptr<Packet> p, Connection *conn_ptr,
    volatile evutil_socket_t &server_fd) {
  if (server_fd <= 0) {
    if (!conn_ptr->ConnectBackend()) {
      std::cerr << "Fall back to EmergencyServe\n";
      EmergencyServe(std::move(p), conn_ptr);
      return;
    }
  }
  write(server_fd, p->buffer->data(), p->buffer->size());
}

void MemcachedService::EmergencyServe(std::unique_ptr<Packet> p,
                                      Connection *conn_ptr) {
  const auto req = ParsedPacket(*p);
  if (req.header.magic != 0x80) {
    std::cerr << "Unsupported Protocol Version:\n" << req << std::endl;
    // TODO: error handling
    exit(1);
  }

  const auto opcode_option = p->GetOpcode();
  if (!opcode_option.has_value()) {
    std::cerr << "Unknow opcode: " << (*p->buffer)[1] << std::endl;
    exit(1);  // TODO: handle it
  }
  const auto opcode = opcode_option.value();

  ParsedPacket resp;
  resp.header.magic = 0x81;
  resp.header.opaque = req.header.opaque;
  resp.header.opcode = req.header.opcode;
  bool is_quite = false;
  CacheEntry entry{
      .value = req.value, .flags = req.extra, .CAS = req.header.CAS};
  // TODO: support CAS, Expiration, TTL, error, and other operations
  switch (opcode) {
    case Header::Opcode::kSet:
      entry.flags->resize(4);
      if (!cache_.Add(*req.key, entry) && !cache_.Replace(*req.key, entry)) {
        resp.header.status = 0x0005;
        break;
      }
      break;
    case Header::Opcode::kAdd:
      entry.value = req.value;
      entry.flags->resize(4);
      if (!cache_.Add(*req.key, entry)) {
        resp.header.status = 0x0005;
        break;
      }
      break;
    case Header::Opcode::kReplace:
      entry.value = req.value;
      entry.flags->resize(4);
      if (!cache_.Replace(*req.key, entry)) {
        resp.header.status = 0x0005;
      }
      break;
    case Header::Opcode::kNoOp:
      break;
    case Header::Opcode::kGetKQ:
      is_quite = true;
    case Header::Opcode::kGetK:
      resp.key = req.key;
    case Header::Opcode::kGet:
      if (!cache_.Get(*req.key, entry)) {
        resp.header.status = 0x0001;
        resp.value = std::make_shared<std::vector<uint8_t>>(Header::kNotFound);
      } else {
        resp.value = entry.value;
        resp.header.CAS = entry.CAS;
        resp.extra = entry.flags;
        resp.header.extras_length = 4;
      }
      resp.header.key_length = resp.key->size();
      resp.header.total_body_length = resp.value->size() +
                                      resp.header.key_length +
                                      resp.header.extras_length;
      break;
    case Header::Opcode::kQuit:
      conn_ptr->FlushBuffer();
      delete conn_ptr;
      return;
    default:
      // TODO: more operations
      std::cerr << "Unsupported Opcode:\n" << req << std::endl;
      exit(1);
  }
  const auto buffer = resp.ToBuffers();
  conn_ptr->response_buffer_->insert(conn_ptr->response_buffer_->end(),
                                     buffer.begin(), buffer.end());
  if (!is_quite) conn_ptr->FlushBuffer();
}

void MemcachedService::Replay() {
  // TODO: How to deal with the UPDATE/DELETE operation during syncing?
  // TODO: parallel?
  int backend_fd, tries = 0;
  while ((backend_fd = Connection::TryConnectBackend(backend_addr_, backend_port_)) == -1) {
    if (tries++ > 100) {
      std::cerr << "Replay failed to connect to backend\n";
      return;
    }
  }
  std::cerr << "Replay connected to backend in " << tries << " tries\n";
  size_t cnt = 0;
  ParsedPacket req;
  static std::vector<uint8_t> expiry(4, 0); // TODO: use real one
  req.header.magic = 0x80;
  req.header.opcode = magic_enum::enum_underlying(Header::Opcode::kSetQ);
  cache_.ConstVisitAll([&](const auto &key, const auto &entry) {
    cnt++;
    req.key = std::make_shared<std::vector<uint8_t>>(key);
    req.value = entry.value;
    req.extra = entry.flags;
    req.header.CAS = entry.CAS;
    req.header.extras_length = 8;
    req.header.key_length = req.key->size();
    req.header.total_body_length =
        req.value->size() + req.header.key_length + req.header.extras_length;
    req.buffer->clear();
    auto buffer = req.ToBuffers();
    buffer.insert(buffer.begin() + 28, expiry.begin(), expiry.end());
    write(backend_fd, buffer.data(), buffer.size());
  });
  close(backend_fd);
  std::cerr << "Replay " << cnt << " items\n";
  // TODO: How to deal with in-flight requests and response buffer?
}

void MemcachedService::BackendHandler(evutil_socket_t fd, short which,
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