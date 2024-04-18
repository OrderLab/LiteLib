#include "packet.hpp"

#include <iostream>

#include "parser.hpp"

void RESPSimpleString::AppendToBuffer(std::vector<uint8_t> &buffer) {
  buffer.reserve(buffer.size() + value->size() + 3);
  buffer.push_back('+');
  buffer.insert(buffer.end(), value->begin(), value->end());
  buffer.push_back('\r');
  buffer.push_back('\n');
}

void RESPError::AppendToBuffer(std::vector<uint8_t> &buffer) {
  buffer.reserve(buffer.size() + value->size() + 3);
  buffer.push_back('-');
  buffer.insert(buffer.end(), value->begin(), value->end());
  buffer.push_back('\r');
  buffer.push_back('\n');
}

void RESPInteger::AppendToBuffer(std::vector<uint8_t> &buffer) {
  auto value = std::to_string(this->value);
  buffer.reserve(buffer.size() + value.size() + 3);
  buffer.push_back(':');
  buffer.insert(buffer.end(), value.begin(), value.end());
  buffer.push_back('\r');
  buffer.push_back('\n');
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

lite::DeserializeResult Packet::Deserialize(InputIterator &begin,
                                            InputIterator end) {
  const auto result = parser->Deserialize(begin, end);
  command = std::make_unique<RESPType>(std::move(*parser->value_));
  if (result == lite::kGood) parser = nullptr;
  return result;
}