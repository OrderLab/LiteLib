#pragma once

#include <cstdint>
#include <lite.hpp>

extern SharedMemory *shm;

struct Packet {
  size_t len = 0, current_line_start = 0;
  enum class Operation {
    kSet,
    kSetResp,
    kGet,
    kGetRespStart,
    kGetRespEnd,
    kQuit,
    kVersion,
    kUnknown,
  } operation = Operation::kUnknown;
  static const uint8_t uninitialized_line_cnt = -1;
  uint8_t line_cnt = 0, expected_line_cnt = uninitialized_line_cnt;
  ShmSharedPtr<ShmVector<uint8_t>> buffer;

  Packet(ShmVoidAllocator allocator = shm->get_segment_manager())
      : buffer(ShmMakeShared(
            shm->get_segment_manager()->template construct<ShmVector<uint8_t>>(
                bip::anonymous_instance)(allocator),
            *shm)) {}

  Packet(ShmSharedPtr<ShmVector<uint8_t>> buffer) : buffer(buffer) {}

 private:
  lite::DeserializeResult GetLine(uint8_t *&begin, uint8_t *end) {
    if (!len || (*buffer)[len - 1] != '\r') {
      while (begin != end) {
        buffer->push_back(*begin);
        len++;
        if (*begin++ == '\r') break;
      }
    }
    while (begin != end) {
      buffer->push_back(*begin);
      len++;
      if (*begin++ == '\n') break;
    }
    if ((*buffer)[len - 1] != '\n')
      return lite::DeserializeResult::kIndeterminate;
    line_cnt++;
    return lite::DeserializeResult::kGood;
  }

  bool VecPartialCmp(const ShmVector<uint8_t> &a, int start_idx,
                     const char b[]) {
    auto len = strlen(b);
    if (a.size() - start_idx < len) return false;
    for (size_t i = 0; i < len; i++) {
      if (a[i + start_idx] != b[i]) return false;
    }
    return true;
  }

  void ParseOperation(int start_idx) {
    if (VecPartialCmp(*buffer, start_idx, "set")) {
      operation = Operation::kSet;
      expected_line_cnt = 2;
      return;
    }
    if (VecPartialCmp(*buffer, start_idx, "get")) {
      operation = Operation::kGet;
      expected_line_cnt = 1;
      return;
    }
    if (VecPartialCmp(*buffer, start_idx, "quit")) {
      operation = Operation::kQuit;
      expected_line_cnt = 1;
      return;
    }
    if (VecPartialCmp(*buffer, start_idx, "STORED") ||
        VecPartialCmp(*buffer, start_idx, "NOT_STORED")) {
      operation = Operation::kSetResp;
      expected_line_cnt = 1;
      return;
    }
    if (VecPartialCmp(*buffer, start_idx, "VALUE")) {
      operation = Operation::kGetRespStart;
      return;
    }
    if ((!start_idx || operation == Operation::kGetRespStart) &&
        VecPartialCmp(*buffer, start_idx, "END")) {
      operation = Operation::kGetRespEnd;
      expected_line_cnt = line_cnt;
      return;
    }
    if (VecPartialCmp(*buffer, start_idx, "VERSION") ||
        VecPartialCmp(*buffer, start_idx, "version")) {
      operation = Operation::kVersion;
      expected_line_cnt = 1;
      return;
    }
    if (operation != Operation::kGetRespStart) operation = Operation::kUnknown;
  }

 public:
  lite::DeserializeResult Deserialize(uint8_t *&begin, uint8_t *end) {
    buffer->reserve(end - begin + buffer->size());
    while (line_cnt < expected_line_cnt) {
      auto res = GetLine(begin, end);
      if (res == lite::DeserializeResult::kIndeterminate) return res;
      if (expected_line_cnt == uninitialized_line_cnt) {
        ParseOperation(current_line_start);
        if (operation == Operation::kUnknown) {
          std::string buffer_str(buffer->begin(), buffer->end());
          LOG(ERROR) << "Unknown operation: " << buffer_str << std::endl;
          return lite::DeserializeResult::kBad;
        }
      }
      current_line_start = len;
    }
    return lite::DeserializeResult::kGood;
  }

  ShmSharedPtr<ShmVector<uint8_t>> Serialize() const { return buffer; }
};
