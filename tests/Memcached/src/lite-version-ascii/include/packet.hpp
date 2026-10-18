#pragma once

#include "../../ascii_protocol.hpp"

struct Packet : memcached_ascii::Decoder {
  std::shared_ptr<std::vector<uint8_t>> buffer;

  Packet() : buffer(std::make_shared<std::vector<uint8_t>>()) {}
  Packet(std::shared_ptr<std::vector<uint8_t>> buffer) : buffer(buffer) {}

  lite::DeserializeResult Deserialize(uint8_t *&begin, uint8_t *end) {
    return Decode(*buffer, begin, end);
  }

  std::shared_ptr<std::vector<uint8_t>> Serialize() const { return buffer; }
};
