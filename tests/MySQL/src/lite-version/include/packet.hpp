#pragma once

#include <lite.hpp>

#include "mysql-server/com_data.hpp"

class Packet {
  using InputIterator = uint8_t *;

  enum State {
    kReadingHeader,
    kReadingPayload,
  } parsing_state_ = kReadingHeader;

 public:
  uint32_t payload_length_ = 0;
  uint8_t sequence_id_ = 0;

  std::shared_ptr<std::vector<uint8_t>> buffer;

  lite::DeserializeResult Deserialize(InputIterator &begin, InputIterator end) {
    while (begin != end) {
      switch (parsing_state_) {
        case kReadingHeader:
          payload_length_ = begin[0] + begin[1] * 256 + begin[2] * 256 * 256;
          sequence_id_ = begin[3];
          buffer = std::make_shared<std::vector<uint8_t>>(begin, begin + 4);
          begin += 4;
        case kReadingPayload:
          size_t remaining_payload_length =
              payload_length_ - (buffer->size() - 4);
          if (end - begin < remaining_payload_length) {
            buffer->insert(buffer->end(), begin, end);
            begin = end;
            return lite::DeserializeResult::kIndeterminate;
          }
          buffer->insert(buffer->end(), begin,
                         begin + remaining_payload_length);
          begin += remaining_payload_length;
          return lite::DeserializeResult::kGood;
      }
    }
    return lite::DeserializeResult::kIndeterminate;
  }

  std::shared_ptr<std::vector<uint8_t>> Serialize() const { return buffer; }
};