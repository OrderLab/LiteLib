#include "dissect.hpp"

#include <lite.hpp>

#include "mysql-server/protocol_classic.hpp"

// https://github.com/wireshark/wireshark/blob/c7745b4dde687f90450f3cbf3a1601df3eeecf7f/epan/dissectors/packet-mysql.c#L3019
bool ResponseDissector::Digest(
    std::shared_ptr<Packet> resp,
    std::vector<std::shared_ptr<Packet>> &responses) {
  if (state_ == END) {
    inter_eof_cnt = 0;
    responses.clear();
  }
  responses.push_back(resp);

  if (state_ == INIT) {
    state_ = END;
    return true;  // server greeting
  }

  uint8_t response_code = (*resp->buffer)[4];

  switch (response_code) {
    case 0xff:
      state_ = END;
      return true;
    case 0x00:
      if (resp->buffer->size() == 11) {
        state_ = END;
        return true;
      } else if (resp->buffer->size() ==
                 16) {  // OK of request prepared statement
        const uint16_t number_of_fields = uint2korr(&((*resp->buffer)[9]));
        const uint16_t number_of_params = uint2korr(&((*resp->buffer)[11]));
        if (number_of_fields > 0 || number_of_params > 0) {
          state_ = NORMAL;
          return false;
        } else {
          state_ = END;
          return true;
        }
      } else {  // has augmented packet
        state_ = NORMAL;
        return false;
      }
    case 0xfe:
      if (resp->buffer->size() > 9) {
        // NOT a EOF Packet
        state_ = END;
        return true;  // ignore AUTH_SWITCH_REQUEST
      }
      if (state_ == BEFORE_INTERMEDIATE_EOF && inter_eof_cnt < 1) {
        inter_eof_cnt++;
        state_ = NORMAL;
        return false;
      } else {
        state_ = END;
        return true;
      }
    case 0x03:
      state_ = BEFORE_INTERMEDIATE_EOF;
      return false;
    default:
      return false;
  }
}

// tests/MySQL/src/mysql-server/libmysql/libmysql.c
Type FetchType(const uint8_t *&payload) {
  Type ret;
  ret.type = (enum_field_types) * (payload++);
  switch (ret.type) {
    case MYSQL_TYPE_TINY:
    case MYSQL_TYPE_SHORT:
    case MYSQL_TYPE_LONG:
    case MYSQL_TYPE_LONGLONG:
      ret.flags = *(payload++);
      break;
    default:
      LOG(WARNING) << "Unsupported MySQL type: " << ret.type << std::endl;
      break;
  }
  return ret;
}

Value FetchValue(const uint8_t *&payload, const Type &type) {
  Value ret;
  switch (type.type) {
    case MYSQL_TYPE_TINY:
      if (type.flags) {  // unsigned
        ret.uint8 = *(payload++);
      } else {
        ret.int8 = *(payload++);
      }
      break;
    case MYSQL_TYPE_SHORT:
      if (type.flags) {  // unsigned
        ret.uint16 = uint2korr(payload);
      } else {
        ret.int16 = sint2korr(payload);
      }
      payload += 2;
      break;
    case MYSQL_TYPE_LONG:
      if (type.flags) {  // unsigned
        ret.uint32 = uint4korr(payload);
      } else {
        ret.int32 = sint4korr(payload);
      }
      payload += 4;
      break;
    case MYSQL_TYPE_LONGLONG:
      if (type.flags) {  // unsigned
        ret.uint64 = uint8korr(payload);
      } else {
        ret.int64 = sint8korr(payload);
      }
      payload += 8;
      break;
    default:
      break;
  }
  return ret;
}

std::string SerializeValue(const Value &value, const Type &type) {
  std::string ret;
  switch (type.type) {
    case MYSQL_TYPE_TINY:
      if (type.flags) {  // unsigned
        ret = std::to_string(value.uint8);
      } else {
        ret = std::to_string(value.int8);
      }
      break;
    case MYSQL_TYPE_SHORT:
      if (type.flags) {  // unsigned
        ret = std::to_string(value.uint16);
      } else {
        ret = std::to_string(value.int16);
      }
      break;
    case MYSQL_TYPE_LONG:
      if (type.flags) {  // unsigned
        ret = std::to_string(value.uint32);
      } else {
        ret = std::to_string(value.int32);
      }
      break;
    case MYSQL_TYPE_LONGLONG:
      if (type.flags) {  // unsigned
        ret = std::to_string(value.uint64);
      } else {
        ret = std::to_string(value.int64);
      }
      break;
    default:
      break;
  }
  return ret;
}

// https://dev.mysql.com/doc/dev/mysql-server/8.0.37/page_protocol_com_stmt_execute.html
std::pair<decltype(ConnectionInfo::prepared_statements)::iterator,
          std::vector<Value>>
DissectExecuteStatement(const uint8_t *payload, ConnectionInfo &conn_info) {
  std::vector<Value> values;
  auto statement_id = uint4korr(payload);
  payload += 4;
  auto it = conn_info.prepared_statements.find(statement_id);
  if (it == conn_info.prepared_statements.end()) {
    LOG(WARNING) << "Unknown statement id: " << statement_id << std::endl;
    return {it, values};
  }
  auto &prepared_statement = it->second;

  payload += 5;                                       // flags + iterations
  payload += (prepared_statement.param_num + 7) / 8;  // null bitmap

  bool new_params_bind_flag = *(payload++);
  if (new_params_bind_flag) {  // st_mysql_bind
    prepared_statement.types.clear();
    for (uint8_t i = 0; i < prepared_statement.param_num; i++) {
      auto type = FetchType(payload);
      prepared_statement.types.push_back(type);
    }
  }

  for (uint8_t i = 0; i < it->second.param_num; i++) {
    auto value = FetchValue(payload, it->second.types[i]);
    values.push_back(value);
  }

  return {it, values};
}