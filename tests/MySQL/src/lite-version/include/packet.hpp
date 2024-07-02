#pragma once

#include <lite.hpp>

#include "mysql-server/com_data.hpp"

class Packet {
  using InputIterator = uint8_t *;

  enum State {
    kReadingHeader0,
    kReadingHeader1,
    kReadingHeader2,
    kReadingHeader3,
    kReadingPayload,
  } parsing_state_ = kReadingHeader0;

 public:
  uint32_t payload_length_ = 0;
  uint8_t sequence_id_ = 0;

  std::shared_ptr<std::vector<uint8_t>> buffer;

  lite::DeserializeResult Deserialize(InputIterator &begin, InputIterator end) {
    while (begin != end) {
      switch (parsing_state_) {
        case kReadingHeader0:
          buffer = std::make_shared<std::vector<uint8_t>>(begin, begin + 1);
          payload_length_ = *begin++;
          parsing_state_ = kReadingHeader1;
          break;
        case kReadingHeader1:
          buffer->push_back(*begin);
          payload_length_ += *begin++ << 8;
          parsing_state_ = kReadingHeader2;
          break;
        case kReadingHeader2:
          buffer->push_back(*begin);
          payload_length_ += *begin++ << 16;
          parsing_state_ = kReadingHeader3;
          break;
        case kReadingHeader3:
          buffer->push_back(*begin);
          sequence_id_ = *begin++;
          parsing_state_ = kReadingPayload;
          break;
        case kReadingPayload:
          size_t remaining_payload_length =
              payload_length_ - (buffer->size() - 4);
          if (end - begin < remaining_payload_length) {
            buffer->insert(buffer->end(), begin, end);
            begin = end;
            break;
          }
          buffer->insert(buffer->end(), begin,
                         begin + remaining_payload_length);
          begin += remaining_payload_length;
          return lite::DeserializeResult::kGood;
      }
    }
    return lite::DeserializeResult::kIndeterminate;
  }

  std::shared_ptr<std::vector<uint8_t>> Serialize() const {
    return buffer;
  }
};