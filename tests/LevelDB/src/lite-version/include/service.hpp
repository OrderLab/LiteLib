#pragma once

#include <event.h>

#include <lite.hpp>
#include <memory>
#include <string>

#include "packet.hpp"

class Connection;

class LevelDBService {
 public:
  LevelDBService(const size_t &max_item_count, std::string &backend_addr,
                 std::string &backend_port);

  bool Filter(const std::shared_ptr<Packet> &p) const;

  void NormalUpdate(const std::shared_ptr<Packet> &p);

  void NormalForwardAndProxyBack(std::shared_ptr<Packet> p,
                                 Connection *conn_ptr,
                                 volatile evutil_socket_t &server_fd);

  void EmergencyServe(std::shared_ptr<Packet> p, Connection *conn_ptr);

  void Replay();

  static void BackendHandler(evutil_socket_t fd, short which, void *arg_conn);

 private:
  std::string &backend_addr_, &backend_port_;

  struct CacheEntry {
    std::shared_ptr<std::string> value = nullptr;
    size_t GetSize() const { return (value ? value->size() : 0); }
  };
  lite::Cache<std::string, CacheEntry> cache_;

  void NormalUpdateImpl(const std::shared_ptr<Packet> &p);

  RESPType *EmergencyServeImpl(std::shared_ptr<Packet> p, Connection *conn_ptr);
};

using LevelDBLiteServer =
    lite::LiteServer<LevelDBService, std::shared_ptr<Packet>, Connection *,
                     evutil_socket_t &>;