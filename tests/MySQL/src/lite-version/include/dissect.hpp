#pragma once

#include <hsql/SQLParser.h>

#include <variant>

#include "mysql-server/binary_log_types.hpp"
#include "packet.hpp"

#define int2pointer(A) (reinterpret_cast<uint8_t *>(&(A)))

class ResponseDissector {
  int8_t inter_eof_cnt = 0;

 public:
  typedef enum mysql_state {
    INIT,
    NORMAL,
    BEFORE_INTERMEDIATE_EOF,
    END,
  } mysql_state_t;

  mysql_state_t state_ = INIT;
  bool Digest(std::shared_ptr<Packet> resp,
              std::vector<std::shared_ptr<Packet>>
                  &responses);  // true: end of the response
};

enum Type {
  kLL,
  kULL,
  kVARCHAR,

  kUnknown,  // should be the last one
};
using Value = std::variant<int64_t, uint64_t, std::string>;

Value operator+(const Value &lhs, const Value &rhs);

bool ValueCast(Value &value, const Type &type);

Type FetchType(const uint8_t *&payload);

Value FetchValue(const uint8_t *&payload, const Type &type);

std::string ValueToString(const Value &value);

std::vector<uint8_t> ValueToNetworkBuffer(const Value &value);

bool ExprToValue(hsql::Expr *expr, Value &value);

struct PreparedStatement {
  uint8_t param_num;
  std::string query;
  std::vector<Type> types;
};

struct ConnectionInfo {
  enum State {
    Init,
    ServerGreeted,
    LoggedIn,
  } state = Init;

  // Normal
  ResponseDissector response_dissector;
  std::vector<std::shared_ptr<Packet>> responses;
  std::unordered_map<uint32_t, PreparedStatement> prepared_statements;

  // Emergency
  std::vector<uint8_t> request_payload;
};

std::pair<decltype(ConnectionInfo::prepared_statements)::iterator,
          std::vector<Value>>
DissectExecuteStatement(const uint8_t *payload, ConnectionInfo &conn_info);