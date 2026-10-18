#include "service.hpp"

#include "connection.hpp"

Memcached::Memcached() {
  std::vector<uint8_t> stored_resp = {'S', 'T', 'O', 'R', 'E', 'D', '\r', '\n'};
  stored.buffer = std::make_shared<std::vector<uint8_t>>(stored_resp);
  std::vector<uint8_t> not_stored_resp = {'N', 'O', 'T', '_', 'S',  'T',
                                          'O', 'R', 'E', 'D', '\r', '\n'};
  not_stored.buffer = std::make_shared<std::vector<uint8_t>>(not_stored_resp);
  null_resp.buffer = std::make_shared<std::vector<uint8_t>>();
  std::vector<uint8_t> version_resp = {'V', 'E', 'R', 'S', 'I', 'O', 'N', ' ',
                                       '1', '.', '6', '.', '1', '4', '\r', '\n'};
  version.buffer = std::make_shared<std::vector<uint8_t>>(version_resp);
}

std::pair<std::vector<std::shared_ptr<Packet>>, bool> Memcached::Match(
    const std::shared_ptr<Packet> &resp, ConnectionInfo &_,
    lite::ThreadSafeQueue<std::pair<std::shared_ptr<Packet>, bool>>
        &pending_requests) const {
  std::vector<std::shared_ptr<Packet>> ret;
  bool forward;
  if (pending_requests.empty()) {
    LOG(ERROR) << "No pending requests" << std::endl;
    return std::make_pair(ret, true);
  }
  auto pair = pending_requests.pop_front();
  auto req = pair.first;
  forward = pair.second;
  ret.push_back(req);
  return std::make_pair(ret, forward);
}

void Memcached::NormalUpdate(const std::shared_ptr<Packet> &resp,
                             std::vector<std::shared_ptr<Packet>> requests,
                             ConnectionInfo &_, Cache *cache) {
  if (requests.empty()) return;

  for (const auto &req : requests) {
    switch (req->operation) {
      case Packet::Operation::kSet:
        if (!resp->buffer ||
            std::string_view(reinterpret_cast<const char *>(resp->buffer->data()),
                             resp->buffer->size()) != "STORED\r\n")
          break;
        EmergencyServeImpl(req, cache, nullptr, false);
        break;
      case Packet::Operation::kGet:
      case Packet::Operation::kQuit:
        break;
      case Packet::Operation::kVersion:
        version.buffer = resp->buffer;
        break;
      default:
        std::string buffer_str(req->buffer->begin(), req->buffer->end());
        LOG(ERROR) << "Unknown operation: " << buffer_str << std::endl;
    }
  }
}

void Memcached::HandleReplayResponse(
    const std::shared_ptr<Packet> &resp,
    std::vector<std::shared_ptr<Packet>> requests, ConnectionInfo &_,
    Cache *cache) const {
  return;
}

std::pair<Packet, bool> Memcached::EmergencyServe(std::shared_ptr<Packet> p,
                                                  ConnectionInfo &conn_info,
                                                  Cache *cache, Logger *logger,
                                                  bool flow_control) const {
  return EmergencyServeImpl(p, cache, logger, flow_control);
}

std::pair<Packet, bool> Memcached::EmergencyServeImpl(std::shared_ptr<Packet> p,
                                                      Cache *cache,
                                                      Logger *logger,
                                                      bool flow_control) const {
  if (flow_control) {
    return {Packet(nullptr), true};
  }
  if (!p->buffer || !p->Complete(*p->buffer)) {
    LOG(ERROR) << "Refusing incomplete Memcached request";
    return {null_resp, true};
  }

  switch (p->operation) {
    case Packet::Operation::kSet: {
      if (!p->StorageComplete(*p->buffer)) {
        LOG(ERROR) << "Invalid Memcached storage bounds";
        return {null_resp, true};
      }
      CacheEntry entry;
      std::vector<uint8_t> key(p->buffer->begin() + p->key_start,
                              p->buffer->begin() + p->key_start + p->key_size);
      entry.flags = std::make_shared<std::vector<uint8_t>>(
          p->buffer->begin() + p->flags_start,
          p->buffer->begin() + p->flags_start + p->flags_size);
      entry.value = std::make_shared<std::vector<uint8_t>>(
          p->buffer->begin() + p->value_start,
          p->buffer->begin() + p->value_start + p->value_size);

      if (!cache->Add(key, entry) && !cache->Replace(key, entry)) {
        if (!logger) LOG(ERROR) << "Failed to set in normal mode" << std::endl;
        return {not_stored, false};
      }
      return {stored, false};
    }
    case Packet::Operation::kGet: {
      Packet resp;
      CacheEntry entry;
      memcached_ascii::Tokens tokens(std::string_view(
          reinterpret_cast<const char *>(p->buffer->data()), p->len - 2));
      tokens.Next();
      for (auto token = tokens.Next(); !token.empty(); token = tokens.Next()) {
        std::vector<uint8_t> key(token.begin(), token.end());
        if (cache->Get(key, entry)) {
          const auto size_str = std::to_string(entry.value->size());
          static const std::vector<uint8_t> get_resp = {'V', 'A', 'L',
                                                        'U', 'E', ' '};
          resp.buffer->reserve(resp.buffer->size() + get_resp.size() +
                               key.size() + entry.flags->size() +
                               size_str.size() + entry.value->size() + 6);
          resp.buffer->insert(resp.buffer->end(), get_resp.begin(),
                              get_resp.end());
          resp.buffer->insert(resp.buffer->end(), key.begin(), key.end());
          resp.buffer->push_back(' ');
          resp.buffer->insert(resp.buffer->end(), entry.flags->begin(),
                              entry.flags->end());
          resp.buffer->push_back(' ');
          resp.buffer->insert(resp.buffer->end(), size_str.begin(),
                              size_str.end());
          resp.buffer->push_back('\r');
          resp.buffer->push_back('\n');
          resp.buffer->insert(resp.buffer->end(), entry.value->begin(),
                              entry.value->end());
          resp.buffer->push_back('\r');
          resp.buffer->push_back('\n');
        }
      }
      static const std::vector<uint8_t> get_resp = {'E', 'N', 'D', '\r', '\n'};
      resp.buffer->insert(resp.buffer->end(), get_resp.begin(), get_resp.end());
      return {resp, false};
    }
    case Packet::Operation::kQuit:
      return {null_resp, true};
    case Packet::Operation::kVersion:
      return {version, false};
    default: {
      std::string buffer_str(p->buffer->begin(), p->buffer->end());
      LOG(ERROR) << "Unsupported Opcode:\n" << buffer_str << std::endl;
      return {null_resp, true};
    }
  }
}
