#pragma once

#include <lite.hpp>

extern SharedMemory *shm;

class Packet {
  using InputIterator = uint8_t *;

  ShmSharedPtr<ShmVector<uint8_t>> buffer;

  ShmVoidAllocator allocator_;

 public:
  Packet(ShmVoidAllocator allocator) : allocator_(allocator) {}

  lite::DeserializeResult Deserialize(InputIterator &begin, InputIterator end) {
    buffer = ShmMakeShared(shm->template construct<ShmVector<uint8_t>>(
                               bip::anonymous_instance)(allocator_),
                           *shm);
    buffer->insert(buffer->begin(), begin, end);
    begin = end;
    return lite::DeserializeResult::kGood;
  }

  ShmSharedPtr<ShmVector<uint8_t>> Serialize() const { return buffer; }
};