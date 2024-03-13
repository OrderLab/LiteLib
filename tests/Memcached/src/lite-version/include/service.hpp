#pragma once

#include <event.h>

#include <lite.hpp>
#include <memory>

#include "packet.hpp"

class Connection;

class MemcachedService {
 public:
  MemcachedService(const size_t &max_item_count);

  bool Filter(const std::unique_ptr<Packet> &p) const;

  void NormalUpdate(const std::unique_ptr<Packet> &p);

  void NormalForwardAndProxyBack(std::unique_ptr<Packet> p, Connection *_,
                                 const evutil_socket_t server_fd) const;

  void EmergencyServe(std::unique_ptr<Packet> p, Connection *conn_ptr);

  void Replay();

  static void BackendHandler(evutil_socket_t fd, short which, void *arg_conn);

 private:
  struct CacheEntry {
    std::shared_ptr<std::vector<uint8_t>> value = nullptr;
    std::shared_ptr<std::vector<uint8_t>> flags = nullptr;
    uint64_t CAS;
    size_t GetSize() const {
      return (value ? value->size() : 0) + (flags ? flags->size() : 0) +
             sizeof(CAS);
    }
  };
  lite::Cache<std::vector<uint8_t>, CacheEntry> cache_;
};

using MemcachedLiteServer =
    lite::LiteServer<MemcachedService, std::unique_ptr<Packet>, Connection *,
                     evutil_socket_t>;