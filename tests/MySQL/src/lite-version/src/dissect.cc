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
    state_ = NORMAL;
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
      } else if ((*resp->buffer)[3] == 0x01) {  // packet number
        state_ = END;
        return true;
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

Value operator+(const Value &lhs, const Value &rhs) {
  return std::visit(
      [](auto &&lhs_value, auto &&rhs_value) -> Value {
        using T1 = std::decay_t<decltype(lhs_value)>;
        using T2 = std::decay_t<decltype(rhs_value)>;
        if constexpr (std::is_same_v<T1, T2>) {
          return lhs_value + rhs_value;
        } else {
          LOG(FATAL) << "operator+: Unsupported Value type: "
                     << typeid(T1).name() << " + " << typeid(T2).name()
                     << std::endl;
          return Value{};
        }
      },
      lhs, rhs);
}

bool ValueCast(Value &value, const Type &type) {
  if (value.index() == type) {
    return true;
  }

  switch (type) {
    case kULL:
      if (auto v = std::get_if<int64_t>(&value)) {
        value = static_cast<uint64_t>(*v);
        return true;
      } else if (auto v = std::get_if<std::string>(&value)) {
        value = std::stoull(*v);
        return true;
      }
      return true;
      break;
    case kLL:
      if (auto v = std::get_if<uint64_t>(&value)) {
        value = static_cast<int64_t>(*v);
        return true;
      } else if (auto v = std::get_if<std::string>(&value)) {
        value = std::stoll(*v);
        return true;
      }
      break;
    case kVARCHAR:
      if (auto v = std::get_if<int64_t>(&value)) {
        value = std::to_string(*v);
        return true;
      } else if (auto v = std::get_if<uint64_t>(&value)) {
        value = std::to_string(*v);
        return true;
      }
      break;
    default:
      LOG(WARNING) << "Unsupported Type Cast: " << value.index() << " -> "
                   << type << std::endl;
      break;
  }
  return false;
}

// tests/MySQL/src/mysql-server/libmysql/libmysql.c
Type FetchType(const uint8_t *&payload) {
  const auto type = (enum_field_types) * (payload++);
  uint flags;
  switch (type) {
    case MYSQL_TYPE_LONGLONG:
      flags = *(payload++);
      if (flags)
        return kULL;
      else
        return kLL;
      break;
    default:
      LOG(WARNING) << "Unsupported MySQL type: " << type << std::endl;
  }
  return kUnknown;
}

Value FetchValue(const uint8_t *&payload, const Type &type) {
  Value ret;
  switch (type) {
    case kULL:
      ret = uint8korr(payload);
      payload += 8;
    case kLL:
      ret = sint8korr(payload);
      payload += 8;
      break;
    case kVARCHAR: {
      uint8_t len = *(payload++);
      ret = std::string(reinterpret_cast<const char *>(payload), len);
      payload += len;
      break;
    }
    default:
      LOG(WARNING) << "Unsupported Type type: " << type << std::endl;
      break;
  }
  return ret;
}

template <class... Ts>
struct overloaded : Ts... {
  using Ts::operator()...;
};

std::string ValueToString(const Value &value) {
  return std::visit(overloaded{
                        [](int64_t value) { return std::to_string(value); },
                        [](uint64_t value) { return std::to_string(value); },
                        [](std::string value) { return value; },
                    },
                    value);
}

std::vector<uint8_t> ValueToNetworkBuffer(const Value &value) {
  std::vector<uint8_t> buffer;
  std::visit(overloaded{
                 [&buffer](int64_t value) {
                   buffer.insert(buffer.end(), int2pointer(value),
                                 int2pointer(value) + 8);
                 },
                 [&buffer](uint64_t value) {
                   buffer.insert(buffer.end(), int2pointer(value),
                                 int2pointer(value) + 8);
                 },
                 [&buffer](std::string value) {
                   uint8_t len = value.size();
                   buffer.push_back(len);
                   buffer.insert(buffer.end(), value.begin(), value.end());
                 },
             },
             value);
  return buffer;
}

bool ExprToValue(hsql::Expr *expr, Value &value) {
  switch (expr->type) {
    case hsql::ExprType::kExprLiteralInt:
      value = expr->ival;
      break;
    case hsql::ExprType::kExprLiteralString:
      value = expr->name;
      break;
    default:
      LOG(WARNING) << "Unsupported Expr type: " << expr->type << std::endl;
      return false;
  }
  return true;
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