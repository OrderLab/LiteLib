#pragma once

#include <event.h>

#include <core.hpp>
#include <memory>
#include <string>

#include "connection.hpp"
#include "packet.hpp"

class LevelDBService {
  using Connection = lite::Connection<Packet, LevelDBService>;

 public:
  LevelDBService(const size_t &max_item_count, std::string &backend_addr,
                 std::string &backend_port);

  bool Filter(const std::shared_ptr<Packet> &p, Connection &conn) const;

  void NormalUpdate(const std::shared_ptr<Packet> &p, Connection &conn);

  void NormalForwardAndProxyBack(std::shared_ptr<Packet> p, Connection &conn,
                                 volatile evutil_socket_t &server_fd);

  void EmergencyServe(std::shared_ptr<Packet> p, Connection &conn);

  void Replay();

  static void BackendHandler(evutil_socket_t fd, short which, void *arg_conn);

 private:
  std::string &backend_addr_, &backend_port_;

  struct CacheEntry {
    std::shared_ptr<std::string> value = nullptr;
    size_t GetSize() const { return (value ? value->size() : 0); }
  };
  lite::Cache<std::string, CacheEntry> cache_;

  struct LogEntry {
    std::shared_ptr<Packet> value;
  };
  lite::Logger<LogEntry> logger_;

  void NormalUpdateImpl(const std::shared_ptr<Packet> &p,
                        const bool in_transaction = false);

  RESPType *EmergencyServeImpl(std::shared_ptr<Packet> p, Connection &conn,
                               const bool in_transaction = false);
};
