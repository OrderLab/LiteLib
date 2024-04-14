#include "packet.hpp"

#include <iostream>

RESPType *RESPType::Parse(InputIterator &begin, InputIterator end) {
  switch (*(begin++)) {
    case '+':
      return RESPSimpleString::Parse(begin, end);
    case '-':
      return RESPError::Parse(begin, end);
    case ':':
      return RESPInteger::Parse(begin, end);
    case '$':
      return RESPBulkString::Parse(begin, end);
    case '*':
      return RESPArray::Parse(begin, end);
    default:
      std::cerr << "Unknown RESPType: " << *(begin - 1) << std::endl;
      return nullptr;
  }
}

RESPSimpleString *RESPSimpleString::Parse(InputIterator &begin,
                                          InputIterator end) {
  auto start = begin;
  while (begin != end && *(begin++) != '\r') {
  }
  if (begin == end) {
    std::cerr << "Could not parse simple string: not long enough" << std::endl;
    return nullptr;
  }
  auto value = std::make_shared<std::string>(start, begin);
  if (*(begin++) != '\n') {
    std::cerr << "Could not parse simple string: no \\n" << std::endl;
    return nullptr;
  }
  return new RESPSimpleString(value);
}

void RESPSimpleString::AppendToBuffer(std::vector<uint8_t> &buffer) {
  buffer.reserve(buffer.size() + value->size() + 3);
  buffer.push_back('+');
  buffer.insert(buffer.end(), value->begin(), value->end());
  buffer.push_back('\r');
  buffer.push_back('\n');
}

RESPError *RESPError::Parse(InputIterator &begin, InputIterator end) {
  auto start = begin;
  while (begin != end && *(begin++) != '\r') {
  }
  if (begin == end) {
    std::cerr << "Could not parse error: not long enough" << std::endl;
    return nullptr;
  }
  auto value = std::make_shared<std::string>(start, begin);
  if (*(begin++) != '\n') {
    std::cerr << "Could not parse error: no \\n" << std::endl;
    return nullptr;
  }
  return new RESPError(value);
}

void RESPError::AppendToBuffer(std::vector<uint8_t> &buffer) {
  buffer.reserve(buffer.size() + value->size() + 3);
  buffer.push_back('-');
  buffer.insert(buffer.end(), value->begin(), value->end());
  buffer.push_back('\r');
  buffer.push_back('\n');
}

RESPInteger *RESPInteger::Parse(InputIterator &begin, InputIterator end) {
  bool is_positive = true;
  int64_t value = 0;
  if (*begin == '-') {
    is_positive = false;
    ++begin;
  } else if (*begin == '+') {
    ++begin;
  }
  while (begin != end && *begin != '\r') {
    value = value * 10 + (*(begin++) - '0');
  }
  if (begin == end) {
    std::cerr << "Could not parse integer: not long enough" << std::endl;
    return nullptr;
  }
  begin++;  // take \r
  if (*(begin++) != '\n') {
    std::cerr << "Could not parse integer: no \\n" << std::endl;
    return nullptr;
  }
  return new RESPInteger(value * (is_positive ? 1 : -1));
}

void RESPInteger::AppendToBuffer(std::vector<uint8_t> &buffer) {
  auto value = std::to_string(this->value);
  buffer.reserve(buffer.size() + value.size() + 3);
  buffer.push_back(':');
  buffer.insert(buffer.end(), value.begin(), value.end());
  buffer.push_back('\r');
  buffer.push_back('\n');
}

RESPBulkString *RESPBulkString::Parse(InputIterator &begin, InputIterator end) {
  std::unique_ptr<RESPInteger> length(RESPInteger::Parse(begin, end));
  if (length == nullptr) {
    std::cerr << "Could not parse bulk string length" << std::endl;
    return nullptr;
  }
  auto len = length->value;
  if (len == -1) {
    return new RESPBulkString(nullptr);
  }
  auto start = begin;
  if (begin + len + 2 > end) {
    std::cerr << "Could not parse bulk string: not long enough" << std::endl;
    std::cerr << "  Expected length: " << len
              << " Remaining length: " << end - begin << std::endl;
    return nullptr;
  }
  begin += len;
  if (*(begin++) != '\r') {
    std::cerr << "Could not parse bulk string: no \\r" << std::endl;
    return nullptr;
  }
  if (*(begin++) != '\n') {
    std::cerr << "Could not parse bulk string: no \\n" << std::endl;
    return nullptr;
  }
  return new RESPBulkString(std::make_shared<std::string>(start, begin - 2));
}

void RESPBulkString::AppendToBuffer(std::vector<uint8_t> &buffer) {
  if (!value) {
    buffer.insert(buffer.end(), "$-1\r\n", "$-1\r\n" + 5);
    return;
  }
  auto len = std::to_string(value->size());
  buffer.reserve(buffer.size() + value->size() + len.size() + 5);
  buffer.push_back('$');
  buffer.insert(buffer.end(), len.begin(), len.end());
  buffer.push_back('\r');
  buffer.push_back('\n');
  buffer.insert(buffer.end(), value->begin(), value->end());
  buffer.push_back('\r');
  buffer.push_back('\n');
}

RESPArray *RESPArray::Parse(InputIterator &begin, InputIterator end) {
  auto length = RESPInteger::Parse(begin, end);
  if (length == nullptr) {
    std::cerr << "Could not parse array length" << std::endl;
    return nullptr;
  }
  auto len = length->value;
  if (len < 0) {
    return new RESPArray(nullptr);
  }
  auto value = std::make_shared<std::vector<std::shared_ptr<RESPType>>>();
  for (int i = 0; i < len; ++i) {
    std::shared_ptr<RESPType> type(RESPType::Parse(begin, end));
    if (type == nullptr) {
      std::cerr << "Could not parse array element" << std::endl;
      return nullptr;
    }
    value->push_back(type);
  }
  return new RESPArray(value);
}

void RESPArray::AppendToBuffer(std::vector<uint8_t> &buffer) {
  auto len = std::to_string(value->size());
  buffer.reserve(buffer.size() + len.size() + 3);
  buffer.push_back('*');
  buffer.insert(buffer.end(), len.begin(), len.end());
  buffer.push_back('\r');
  buffer.push_back('\n');
  for (auto &type : *value) {
    type->AppendToBuffer(buffer);
  }
}