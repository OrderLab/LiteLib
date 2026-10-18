#pragma once

#include "../../ascii_protocol.hpp"

extern SharedMemory *shm;

struct Packet : memcached_ascii::Decoder {
  ShmSharedPtr<ShmVector<uint8_t>> buffer;

  Packet(ShmVoidAllocator allocator = shm->get_segment_manager())
      : buffer(ShmMakeShared(
            shm->get_segment_manager()->template construct<ShmVector<uint8_t>>(
                bip::anonymous_instance)(allocator),
            *shm)) {}
  Packet(ShmSharedPtr<ShmVector<uint8_t>> buffer) : buffer(buffer) {}

  lite::DeserializeResult Deserialize(uint8_t *&begin, uint8_t *end) {
    return Decode(*buffer, begin, end);
  }

  ShmSharedPtr<ShmVector<uint8_t>> Serialize() const { return buffer; }
};
