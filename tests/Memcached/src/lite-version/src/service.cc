#include "service.hpp"

#include "connection.hpp"

bool Memcached::Filter(const std::shared_ptr<Packet> &p,
                       ConnectionInfo &_) const {
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

void Memcached::NormalUpdate(const std::shared_ptr<Packet> &p,
                             ConnectionInfo &_, Cache &cache) {
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
      cache.Add(*packet.key, entry);
      break;
    case Header::Opcode::kReplace:
      cache.Replace(*packet.key, entry);
      break;
    case Header::Opcode::kQuit:
      break;
    default:  // TODO: support CAS, Expiration, error and other operations
      std::cerr << "Unknown OpCode: " << magic_enum::enum_name(opcode)
                << std::endl;
  }
}

Packet Memcached::EmergencyServe(std::shared_ptr<Packet> p,
                                 ConnectionInfo &conn_info, Cache &cache,
                                 Logger &logger) {
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
      logger.Log(LogEntry{.packet = p});
      entry.flags->resize(4);
      if (!cache.Add(*req.key, entry) && !cache.Replace(*req.key, entry)) {
        resp.header.status = 0x0005;
        break;
      }
      break;
    case Header::Opcode::kAdd:
      logger.Log(LogEntry{.packet = p});
      entry.value = req.value;
      entry.flags->resize(4);
      if (!cache.Add(*req.key, entry)) {
        resp.header.status = 0x0005;
        break;
      }
      break;
    case Header::Opcode::kReplace:
      logger.Log(LogEntry{.packet = p});
      entry.value = req.value;
      entry.flags->resize(4);
      if (!cache.Replace(*req.key, entry)) {
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
      if (!cache.Get(*req.key, entry)) {
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
      break;
    default:
      // TODO: more operations
      std::cerr << "Unsupported Opcode:\n" << req << std::endl;
      exit(1);
  }
  const auto buffer = resp.Serialize();
  conn_info.response_buffer->insert(conn_info.response_buffer->end(),
                                    buffer->begin(), buffer->end());
  if (!is_quite) {
    Packet p(std::move(conn_info.response_buffer));
    conn_info.response_buffer = std::make_unique<std::vector<uint8_t>>();
    return p;
  }
  return Packet(nullptr);
}
