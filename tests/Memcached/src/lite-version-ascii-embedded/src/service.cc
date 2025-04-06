#include "service.hpp"

#include "connection.hpp"
#include "embedded_full.hpp"
SharedMemory *shm;

Packet *Memcached::stored, *Memcached::not_stored, *Memcached::null_resp,
    *Memcached::version;

RequestDestructorFn Memcached::RequestDestructor;

void Memcached::DelayedConstructor() {
  stored = new Packet();
  std::vector<uint8_t> stored_resp = {'S', 'T', 'O', 'R', 'E', 'D', '\r', '\n'};
  stored->buffer = ShmMakeShared(
      shm->get_segment_manager()->template construct<ShmVector<uint8_t>>(
          bip::anonymous_instance)(shm->get_segment_manager()),
      *shm);
  stored->buffer->assign(stored_resp.begin(), stored_resp.end());

  not_stored = new Packet();
  std::vector<uint8_t> not_stored_resp = {'N', 'O', 'T', '_', 'S',  'T',
                                          'O', 'R', 'E', 'D', '\r', '\n'};
  not_stored->buffer = ShmMakeShared(
      shm->get_segment_manager()->template construct<ShmVector<uint8_t>>(
          bip::anonymous_instance)(shm->get_segment_manager()),
      *shm);
  not_stored->buffer->assign(not_stored_resp.begin(), not_stored_resp.end());

  null_resp = new Packet();
  null_resp->buffer = ShmMakeShared(
      shm->get_segment_manager()->template construct<ShmVector<uint8_t>>(
          bip::anonymous_instance)(shm->get_segment_manager()),
      *shm);

  version = new Packet();
  std::vector<uint8_t> version_resp = {'V', 'E', 'R',  'S', 'I', 'O',
                                       'N', ' ', '1',  '.', '6', '.',
                                       '1', '4', '\r', '\n'};
  version->buffer = ShmMakeShared(
      shm->get_segment_manager()->template construct<ShmVector<uint8_t>>(
          bip::anonymous_instance)(shm->get_segment_manager()),
      *shm);
  version->buffer->assign(version_resp.begin(), version_resp.end());
}

int Memcached::EmbeddedNormalUpdate(void *request, ConnectionInfo &conn,
                                    Cache *cache,
                                    RequestDestructorFn RequestDestructor) {
  lite_item *it = static_cast<lite_item *>(request);

  // LOG(INFO) << "key_len: " << (int)it->nkey << " key: ";
  // for (int i = 0; i < it->nkey; i++) {
  //   fprintf(stderr, "%c", it->key[i]);
  // }
  // fprintf(stderr, "\n");

  // LOG(INFO) << "flags: " << it->flags << std::endl;

  // LOG(INFO) << "value_len: " << it->nbytes << " value: ";
  // for (int i = 0; i < it->nbytes; i++) {
  //   fprintf(stderr, "%d ", it->value[i]);
  // }
  // fprintf(stderr, "\n");

  CacheKey key(shm->get_segment_manager());
  key.assign(it->key, it->key + it->nkey);

  CacheEntry entry;
  entry.flags.assign(it->flags, it->flags + 2);
  entry.value.assign(it->value, it->value + it->nbytes);

  cache->Set(key, entry);

  RequestDestructor(request);
  return 0;
}

std::pair<Packet, bool> Memcached::EmergencyServe(ShmSharedPtr<Packet> p,
                                                  ConnectionInfo &conn_info,
                                                  Cache *cache, Logger *logger,
                                                  bool flow_control) {
  return EmergencyServeImpl(p, cache, logger, flow_control);
}

std::pair<Packet, bool> Memcached::EmergencyServeImpl(ShmSharedPtr<Packet> p,
                                                      Cache *cache,
                                                      Logger *logger,
                                                      bool flow_control) {
  if (flow_control) {
    return {Packet(nullptr), true};
  }

  switch (p->operation) {
    case Packet::Operation::kSet: {
      CacheEntry entry;
      // Parse command line: "set <key> <flags> <exptime> <bytes>\r\n"
      size_t pos = 4;                                          // Skip "set "
      while (pos < p->len && (*p->buffer)[pos] != ' ') pos++;  // Find key end
      CacheKey key(p->buffer->begin() + 4, p->buffer->begin() + pos,
                   shm->get_segment_manager());
      pos++;  // Skip space

      // Parse flags
      size_t flags_start = pos;
      while (pos < p->len && (*p->buffer)[pos] != ' ') pos++;
      entry.flags = ShmVector<uint8_t>(p->buffer->begin() + flags_start,
                                       p->buffer->begin() + pos,
                                       shm->get_segment_manager());
      pos++;  // Skip space

      // Skip exptime
      while (pos < p->len && (*p->buffer)[pos] != ' ') pos++;
      pos++;  // Skip space

      // Parse bytes length
      size_t bytes_start = pos;
      while (pos < p->len && (*p->buffer)[pos] != '\r') pos++;
      std::string bytes_str(p->buffer->begin() + bytes_start,
                            p->buffer->begin() + pos);
      size_t bytes = std::stoul(bytes_str);

      // Skip \r\n
      pos += 2;

      // Get value
      entry.value = ShmVector<uint8_t>(p->buffer->begin() + pos,
                                       p->buffer->begin() + pos + bytes,
                                       shm->get_segment_manager());

      if (!cache->Add(key, entry) && !cache->Replace(key, entry)) {
        if (!logger) LOG(ERROR) << "Failed to set in normal mode" << std::endl;
        return {*not_stored, false};
      }
      return {*stored, false};
    }
    case Packet::Operation::kGet: {
      Packet resp;
      CacheEntry entry;
      size_t pos = 4;
      while ((*p->buffer)[pos] != '\n') {
        size_t key_start = pos;
        while ((*p->buffer)[pos] != ' ' && (*p->buffer)[pos] != '\r') pos++;
        CacheKey key(p->buffer->begin() + key_start, p->buffer->begin() + pos,
                     shm->get_segment_manager());
        if (cache->Get(key, entry)) {
          const auto size_str = std::to_string(entry.value.size());
          static const std::vector<uint8_t> get_resp = {'V', 'A', 'L',
                                                        'U', 'E', ' '};
          resp.buffer->reserve(resp.buffer->size() + get_resp.size() +
                               key.size() + entry.flags.size() +
                               size_str.size() + entry.value.size() + 6);
          resp.buffer->insert(resp.buffer->end(), get_resp.begin(),
                              get_resp.end());
          resp.buffer->insert(resp.buffer->end(), key.begin(), key.end());
          resp.buffer->push_back(' ');
          resp.buffer->insert(resp.buffer->end(), entry.flags.begin(),
                              entry.flags.end());
          resp.buffer->push_back(' ');
          resp.buffer->insert(resp.buffer->end(), size_str.begin(),
                              size_str.end());
          resp.buffer->push_back('\r');
          resp.buffer->push_back('\n');
          resp.buffer->insert(resp.buffer->end(), entry.value.begin(),
                              entry.value.end());
          resp.buffer->push_back('\r');
          resp.buffer->push_back('\n');
        }
        pos++;
      }
      static const std::vector<uint8_t> get_resp = {'E', 'N', 'D', '\r', '\n'};
      resp.buffer->insert(resp.buffer->end(), get_resp.begin(), get_resp.end());
      return {resp, false};
    }
    case Packet::Operation::kQuit:
      return {*null_resp, true};
    case Packet::Operation::kVersion:
      return {*version, false};
    default: {
      std::string buffer_str(p->buffer->begin(), p->buffer->end());
      LOG(ERROR) << "Unsupported Opcode:\n" << buffer_str << std::endl;
      return {*null_resp, true};
    }
  }
}
