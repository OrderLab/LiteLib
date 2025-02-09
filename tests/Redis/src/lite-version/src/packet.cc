#include "packet.hpp"

#include <iostream>

#include "parser.hpp"

bip::managed_shared_memory *shm;

void RESPSimpleString::AppendToBuffer(ShmVector<uint8_t> &buffer) {
  buffer.reserve(buffer.size() + value->size() + 3);
  buffer.push_back('+');
  buffer.insert(buffer.end(), value->begin(), value->end());
  buffer.push_back('\r');
  buffer.push_back('\n');
}

void RESPError::AppendToBuffer(ShmVector<uint8_t> &buffer) {
  buffer.reserve(buffer.size() + value->size() + 3);
  buffer.push_back('-');
  buffer.insert(buffer.end(), value->begin(), value->end());
  buffer.push_back('\r');
  buffer.push_back('\n');
}

void RESPInteger::AppendToBuffer(ShmVector<uint8_t> &buffer) {
  SharedString value(std::to_string(this->value).c_str());
  buffer.reserve(buffer.size() + value.size() + 3);
  buffer.push_back(':');
  buffer.insert(buffer.end(), value.begin(), value.end());
  buffer.push_back('\r');
  buffer.push_back('\n');
}

void RESPBulkString::AppendToBuffer(ShmVector<uint8_t> &buffer) {
  if (!value) {
    const char *null_str = "$-1\r\n";
    buffer.insert(buffer.end(), null_str, null_str + 5);
    return;
  }
  SharedString len(std::to_string(value->size()).c_str());
  buffer.reserve(buffer.size() + value->size() + len.size() + 5);
  buffer.push_back('$');
  buffer.insert(buffer.end(), len.begin(), len.end());
  buffer.push_back('\r');
  buffer.push_back('\n');
  buffer.insert(buffer.end(), value->begin(), value->end());
  buffer.push_back('\r');
  buffer.push_back('\n');
}

void RESPArray::AppendToBuffer(ShmVector<uint8_t> &buffer) {
  auto len = std::to_string(value.size());
  buffer.push_back('*');
  buffer.insert(buffer.end(), len.begin(), len.end());
  buffer.push_back('\r');
  buffer.push_back('\n');
  for (auto &type : value) {
    type->AppendToBuffer(buffer);
  }
}

void RESPNull::AppendToBuffer(ShmVector<uint8_t> &buffer) {
  buffer.insert(buffer.end(), "_\r\n", "_\r\n" + 3);
}

void RESPMap::AppendToBuffer(ShmVector<uint8_t> &buffer) {
  // in resp2, maps are represented as arrays of bulk strings
  //   auto len = std::to_string((*value).size());
  //   buffer.push_back('%');
  //   buffer.insert(buffer.end(), len.begin(), len.end());
  //   buffer.push_back('\r');
  //   buffer.push_back('\n');
  //   for (auto &pair : (*value)) {
  //     pair.first->AppendToBuffer(buffer);
  //     pair.second->AppendToBuffer(buffer);
  //   }
  auto len = std::to_string((*value).size() * 2);
  buffer.push_back('*');
  buffer.insert(buffer.end(), len.begin(), len.end());
  buffer.push_back('\r');
  buffer.push_back('\n');
  for (auto &pair : (*value)) {
    pair.first->AppendToBuffer(buffer);
    pair.second->AppendToBuffer(buffer);
  }
}

void RESPSet::AppendToBuffer(ShmVector<uint8_t> &buffer) {
  auto len = std::to_string((*value).size());
  buffer.push_back('~');
  buffer.insert(buffer.end(), len.begin(), len.end());
  buffer.push_back('\r');
  buffer.push_back('\n');
  for (auto &type : (*value)) {
    type->AppendToBuffer(buffer);
  }
}

lite::DeserializeResult Packet::Deserialize(InputIterator &begin,
                                            InputIterator end) {
  const auto result = parser->Deserialize(begin, end);
  if (result == lite::kGood) {
    command = std::move(parser->value_);
    parser = nullptr;
  }
  return result;
}