#pragma once

#include "mysql-server/binary_log_types.hpp"
#include "packet.hpp"

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

struct Type {
  enum_field_types type;
  uint flags;
};

union Value {
  int8_t int8;
  uint8_t uint8;
  int16_t int16;
  uint16_t uint16;
  int32_t int32;
  uint32_t uint32;
  int64_t int64;
  uint64_t uint64;
};

Type FetchType(const uint8_t *&payload);

Value FetchValue(const uint8_t *&payload, const Type &type);

std::string SerializeValue(const Value &value, const Type &type);

struct PreparedStatement {
  uint8_t param_num;
  std::string query;
  std::vector<Type> types;
};

struct ConnectionInfo {
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