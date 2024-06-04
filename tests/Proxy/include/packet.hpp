#pragma once

#include <lite.hpp>

class Packet {
  using InputIterator = uint8_t *;

  std::shared_ptr<std::vector<uint8_t>> buffer;

 public:
  lite::DeserializeResult Deserialize(InputIterator &begin, InputIterator end) {
    buffer = std::make_shared<std::vector<uint8_t>>(begin, end);
    begin = end;
    return lite::DeserializeResult::kGood;
  }

  std::shared_ptr<std::vector<uint8_t>> Serialize() const { return buffer; }
};