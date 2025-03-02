#include "service.hpp"

#include <unordered_set>

#include "embedded_full.hpp"
#include "packet.hpp"

ShmSharedPtr<RESPPacket> Redis::abort_req_;

RequestDestructorFn Redis::RequestDestructor;

SharedMemory *shm;

ShmSharedPtr<RESPPacket> CacheEntry::ToRequest(const CacheKey &key) const {
  switch (type) {
    case CacheEntryType::STRING: {
      auto ret = ShmMakeShared(
          shm->get_segment_manager()->template construct<RESPPacket>(
              bip::anonymous_instance)(),
          *shm);
      ret->InitResponseArray(3);
      ret->AddResponseArrayElement("SET", 3);
      ret->AddResponseArrayElement(key.c_str(), key.size());
      ret->AddResponseArrayElement(value.c_str(), value.size());
      return ret;
    }
    case CacheEntryType::MAP: {
      auto ret = ShmMakeShared(
          shm->get_segment_manager()->template construct<RESPPacket>(
              bip::anonymous_instance)(),
          *shm);
      ret->InitResponseArray(2 + map_value->size() * 2);
      ret->AddResponseArrayElement("HMSET", 5);
      ret->AddResponseArrayElement(key.c_str(), key.size());
      for (const auto &[field, value] : *map_value) {
        ret->AddResponseArrayElement(field.c_str(), field.size());
        ret->AddResponseArrayElement(value->c_str(), value->size());
      }
      return ret;
    }
  }
  Unreachable();
}

std::optional<RESPPacket> Redis::EmergencyConnectionEstablishHook(
    ConnectionInfo &conn) {
  return std::nullopt;
}

void Redis::DelayedConstructor() {
  abort_req_ =
      ShmMakeShared(shm->get_segment_manager()->template construct<RESPPacket>(
                        bip::anonymous_instance)(),
                    *shm);
  abort_req_->InitResponseArray(1);
  abort_req_->AddResponseArrayElement("DISCARD");
}

int Redis::EmbeddedNormalUpdate(void *void_request, ConnectionInfo &conn,
                                Cache *cache,
                                RequestDestructorFn RequestDestructor) {
  EmbeddedRequest *req = static_cast<EmbeddedRequest *>(void_request);
  Redis::RequestDestructor = RequestDestructor;
  auto req_packet =
      ShmMakeShared(shm->get_segment_manager()->template construct<RESPPacket>(
                        bip::anonymous_instance)(req),
                    *shm);
  HandleRequestForConnection(req_packet, conn, cache);
  return 0;  // TODO: use true return value
}

std::pair<RESPPacket, bool> Redis::EmergencyServe(
    ShmSharedPtr<RESPPacket> req_packet, ConnectionInfo &conn, Cache *cache,
    Logger *logger, bool flow_control) {
  return HandleRequestForConnection(req_packet, conn, cache, logger,
                                    flow_control)
      .value();
}

std::optional<std::pair<RESPPacket, bool>> Redis::HandleRequestForConnection(
    ShmSharedPtr<RESPPacket> req_packet, ConnectionInfo &conn, Cache *cache,
    Logger *logger, bool flow_control) {
  const bool in_emergency = logger;
  auto req = req_packet->ToEmbeddedRequest();
  if (Likely(!conn.is_in_transaction_))
    return HandleSingleRequest(req, cache, false, logger, flow_control);
  if (Unlikely(flow_control)) {
    if (!logger->EraseConnectionLogs(conn.transactions.size() + 1)) {
      LOG(ERROR) << "Failed to undo log\n";
      logger->Log(abort_req_);
    }
    conn.Reset();
    return std::make_pair(RESPPacket::ResponseError("ERR flow control enabled"),
                          false);
  }
  if (Unlikely(req->type == EmbeddedRequestType::kExec)) {
    if (Unlikely(in_emergency &&
                 !logger->EraseConnectionLogs(conn.transactions.size() + 1))) {
      LOG(ERROR) << "Failed to undo log\n";
      logger->Log(abort_req_);
    }
    auto cache_lock = cache->TransactionLock();
    if (Likely(!in_emergency)) {
      for (const auto &r : conn.transactions) {
        HandleSingleRequest(r->ToEmbeddedRequest(), cache, true, logger, false);
      }
      conn.Reset();
      return std::nullopt;
    } else {
      bool shutdown = false;
      auto response = RESPPacket::ResponseArray(conn.transactions.size());
      for (const auto &r : conn.transactions) {
        auto [sub_req, sub_shutdown] =
            HandleSingleRequest(r->ToEmbeddedRequest(), cache, true, logger,
                                false)
                .value();
        response.AddResponseArrayElement(sub_req.buffer->data(),
                                         sub_req.buffer->size());
        shutdown |= sub_shutdown;
      }
      conn.Reset();
      return std::make_pair(std::move(response), shutdown);
    }
  } else if (Unlikely(req->type == EmbeddedRequestType::kDiscard)) {
    if (Unlikely(!in_emergency &&
                 !logger->EraseConnectionLogs(conn.transactions.size() + 1))) {
      LOG(ERROR) << "Failed to undo log\n";
      logger->Log(abort_req_);
    }
    conn.Reset();
    return std::nullopt;
  } else if (Unlikely(req->type == EmbeddedRequestType::kMulti)) {
    conn.is_in_transaction_ = true;
    if (Likely(!in_emergency)) {
      return std::nullopt;
    }
    logger->Log(req_packet);
    return std::make_pair(
        RESPPacket::ResponseSimpleString("OK", shm->get_segment_manager()),
        false);
  }
  conn.transactions.push_back(req_packet);
  if (Likely(!in_emergency)) {
    return std::nullopt;
  }
  logger->Log(req_packet);
  return std::make_pair(RESPPacket::ResponseSimpleString("QUEUED"), false);
}

std::optional<std::pair<RESPPacket, bool>> Redis::HandleSingleRequest(
    EmbeddedRequest *req, Cache *cache, const bool in_transaction,
    Logger *logger, bool flow_control) {
  const bool in_emergency = logger;
  if (Unlikely(req->type == EmbeddedRequestType::kUnknown)) {
    if (in_emergency)
      return std::make_pair(RESPPacket::ResponseError("ERR unknown opcode"),
                            false);
    return std::nullopt;
  }
  if (Unlikely(flow_control))
    return std::make_pair(RESPPacket::ResponseError("ERR flow control enabled"),
                          false);
  CacheEntry cache_entry(shm->get_segment_manager());
  switch (req->type) {
    case EmbeddedRequestType::kHset: {
      char *key = static_cast<char *>(req->argv[1]->ptr);
      ShmString shm_key(key, req->argv_len[1], shm->get_segment_manager());
      CacheKey cache_key(shm_key,
                         ShmAllocator<char>(shm->get_segment_manager()));
      auto map = ShmSharedPtr<MapType>(
          nullptr, shm->get_segment_manager(),
          ShmDeleter<MapType>(shm->get_segment_manager()));
      // get original value
      if (Likely(cache->Get(cache_key, cache_entry, in_transaction))) {
        map = cache_entry.map_value;
      } else {
        map = ShmMakeShared(
            shm->get_segment_manager()->template construct<MapType>(
                bip::anonymous_instance)(shm->get_segment_manager()),
            *shm);
      }
      for (size_t i = 2; i < req->argc; i += 2) {
        char *field = static_cast<char *>(req->argv[i]->ptr);
        char *value = static_cast<char *>(req->argv[i + 1]->ptr);
        ShmString shm_field(field, req->argv_len[i],
                            shm->get_segment_manager());
        auto new_value = ShmMakeUnique(
            shm->get_segment_manager()->template construct<ShmString>(
                bip::anonymous_instance)(value, req->argv_len[i + 1],
                                         shm->get_segment_manager()),
            *shm);

        auto it = map->find(shm_field);
        if (it != map->end()) {
          it->second.reset(new_value.release());
        } else {
          map->insert(std::make_pair(shm_field, boost::move(new_value)));
        }
      }
      cache_entry.SetType(CacheEntryType::MAP);
      cache_entry.map_value = map;
      if (Unlikely(!cache->Set(cache_key, cache_entry, in_transaction))) {
        LOG(ERROR) << "Failed to set key: " << cache_key << std::endl;
        if (in_emergency)
          return std::make_pair(RESPPacket::ResponseError("ERR failed to set"),
                                false);
        return std::nullopt;
      } else {
        return std::make_pair(
            RESPPacket::ResponseSimpleString("OK", shm->get_segment_manager()),
            false);
      }
    }
    case EmbeddedRequestType::kHgetall: {
      assert(in_emergency);
      char *key = static_cast<char *>(req->argv[1]->ptr);
      ShmString shm_key(key, req->argv_len[1], shm->get_segment_manager());
      CacheKey cache_key(shm_key,
                         ShmAllocator<char>(shm->get_segment_manager()));
      if (Likely(cache->Get(cache_key, cache_entry, in_transaction) &&
                 cache_entry.map_value)) {
        auto resp =
            RESPPacket::ResponseArray(cache_entry.map_value->size() * 2);
        for (const auto &[field, value] : *cache_entry.map_value) {
          resp.AddResponseArrayElement(field.c_str(), field.size());
          resp.AddResponseArrayElement(value->c_str(), value->size());
        }
        return std::make_pair(std::move(resp), false);
      }
      return std::make_pair(
          RESPPacket::ResponseNull(shm->get_segment_manager()), false);
    }
    case EmbeddedRequestType::kSet: {
      char *key = static_cast<char *>(req->argv[1]->ptr);
      char *value = static_cast<char *>(req->argv[2]->ptr);
      ShmString shm_key(key, req->argv_len[1], shm->get_segment_manager());
      ShmString shm_value(value, req->argv_len[2], shm->get_segment_manager());
      CacheKey cache_key(shm_key,
                         ShmAllocator<char>(shm->get_segment_manager()));
      cache_entry.SetType(CacheEntryType::STRING);
      cache_entry.value = shm_value;
      if (Likely(!cache->Set(cache_key, cache_entry, in_transaction))) {
        LOG(ERROR) << "Failed to set key: " << cache_key << std::endl;
        if (in_emergency)
          return std::make_pair(RESPPacket::ResponseError("ERR failed to set"),
                                false);
        return std::nullopt;
      } else {
        return std::make_pair(
            RESPPacket::ResponseSimpleString("OK", shm->get_segment_manager()),
            false);
      }
    }
    case EmbeddedRequestType::kGet: {
      assert(in_emergency);
      char *key = static_cast<char *>(req->argv[1]->ptr);
      ShmString shm_key(key, req->argv_len[1], shm->get_segment_manager());
      CacheKey cache_key(shm_key,
                         ShmAllocator<char>(shm->get_segment_manager()));
      if (Likely(cache->Get(cache_key, cache_entry, in_transaction))) {
        return std::make_pair(
            RESPPacket::ResponseBulkString(cache_entry.value.c_str(),
                                           cache_entry.value.size()),
            false);
      }
      return std::make_pair(
          RESPPacket::ResponseNull(shm->get_segment_manager()), false);
    }
    case EmbeddedRequestType::kQuit: {
      assert(in_emergency);
      return std::make_pair(
          RESPPacket::ResponseSimpleString("OK", shm->get_segment_manager()),
          true);
    }
    case EmbeddedRequestType::kPing: {
      assert(in_emergency);
      if (req->argc == 1)
        return std::make_pair(RESPPacket::ResponseSimpleString("PONG"), false);
      else if (req->argc == 2) {
        char *message = static_cast<char *>(req->argv[1]->ptr);
        ShmString shm_message(message, req->argv_len[1],
                              shm->get_segment_manager());
        return std::make_pair(RESPPacket::ResponseBulkString(
                                  shm_message.c_str(), shm_message.size()),
                              false);
      }
      return std::make_pair(
          RESPPacket::ResponseError("ERR wrong number of arguments"), false);
    }
    default:
      LOG(ERROR) << "Unknown request type: " << int(req->type) << std::endl;
      return std::nullopt;
  }
  Unreachable();
}
