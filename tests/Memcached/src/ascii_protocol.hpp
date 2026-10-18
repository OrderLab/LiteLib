#pragma once

#include <algorithm>
#include <charconv>
#include <cstdint>
#include <limits>
#include <string_view>

#include <lite.hpp>

namespace memcached_ascii {

class Tokens {
 public:
  explicit Tokens(std::string_view line) : rest_(line) {}

  std::string_view Next() {
    const auto start = rest_.find_first_not_of(' ');
    if (start == std::string_view::npos) {
      rest_ = {};
      return {};
    }
    rest_.remove_prefix(start);
    const auto end = rest_.find(' ');
    const auto token = rest_.substr(0, end);
    rest_.remove_prefix(token.size());
    return token;
  }

 private:
  std::string_view rest_;
};

template <typename Integer>
bool Number(std::string_view text, Integer &value) {
  if (text.empty()) return false;
  const auto result =
      std::from_chars(text.data(), text.data() + text.size(), value);
  return result.ec == std::errc{} &&
         result.ptr == text.data() + text.size();
}

inline bool Key(std::string_view key) {
  return !key.empty() && key.size() <= 250 &&
         std::all_of(key.begin(), key.end(), [](unsigned char c) {
           return c > 32 && c != 127;
         });
}

// Only offsets/counts are retained: embedded Packets live in shared memory.
class Decoder {
 public:
  enum class Operation {
    kSet, kSetResp, kGet, kGetRespStart, kGetRespEnd, kQuit, kVersion,
    kError, kUnknown,
  } operation = Operation::kUnknown;
  size_t len = 0;
  size_t key_start = 0, key_size = 0, flags_start = 0, flags_size = 0;
  size_t value_start = 0, value_size = 0;

  template <typename Buffer>
  bool Complete(const Buffer &buffer) const {
    return state_ == State::kDone && len == buffer.size();
  }

  template <typename Buffer>
  bool StorageComplete(const Buffer &buffer) const {
    return Complete(buffer) && operation == Operation::kSet &&
           key_start <= len && key_size <= len - key_start &&
           flags_start <= len && flags_size <= len - flags_start &&
           value_start <= len && len - value_start >= 2 &&
           value_size == len - value_start - 2 &&
           buffer[len - 2] == '\r' && buffer[len - 1] == '\n';
  }

  template <typename Buffer>
  lite::DeserializeResult Decode(Buffer &buffer, uint8_t *&begin,
                                 uint8_t *end) {
    if (state_ == State::kBad) return lite::kBad;
    if (state_ == State::kDone) return lite::kGood;
    if (buffer.size() != len) return Bad();
    while (begin != end) {
      if (state_ == State::kBody) {
        const auto count = std::min(remaining_, size_t(end - begin));
        buffer.insert(buffer.end(), begin, begin + count);
        begin += count;
        len += count;
        remaining_ -= count;
        if (remaining_) return lite::kIndeterminate;
        state_ = State::kBodyCR;
        continue;
      }
      const uint8_t c = *begin++;
      buffer.push_back(c);
      ++len;
      if (state_ == State::kBodyCR) {
        if (c != '\r') return Bad();
        state_ = State::kBodyLF;
      } else if (state_ == State::kBodyLF) {
        if (c != '\n') return Bad();
        if (!values_) {
          state_ = State::kDone;
          return lite::kGood;
        }
        line_start_ = len;
        state_ = State::kLine;
      } else {
        if (c == '\n') {
          if (len - line_start_ < 2 || buffer[len - 2] != '\r')
            return Bad();
          const std::string_view line(
              reinterpret_cast<const char *>(buffer.data()) + line_start_,
              len - line_start_ - 2);
          if (!Header(line, buffer.max_size())) return Bad();
          if (state_ == State::kDone) return lite::kGood;
        } else if (len - line_start_ >= 2 && buffer[len - 2] == '\r') {
          return Bad();
        }
      }
    }
    return lite::kIndeterminate;
  }

 private:
  enum class State { kLine, kBody, kBodyCR, kBodyLF, kDone, kBad };
  State state_ = State::kLine;
  size_t line_start_ = 0, remaining_ = 0;
  bool values_ = false;

  lite::DeserializeResult Bad() {
    state_ = State::kBad;
    LOG(ERROR) << "Invalid or unsupported Memcached ASCII frame";
    return lite::kBad;
  }

  bool Header(std::string_view line, size_t max_size) {
    Tokens tokens(line);
    const auto command = tokens.Next();
    if (values_ && command != "VALUE" && command != "END") return false;
    if (command == "set" || command == "add" || command == "replace" ||
        command == "VALUE") {
      const bool value = command == "VALUE";
      const auto key = tokens.Next();
      const auto flags = tokens.Next();
      uint32_t flags_number;
      int64_t expiration;
      if (!Key(key) || !Number(flags, flags_number)) return false;
      if (!value && !Number(tokens.Next(), expiration)) return false;
      size_t bytes;
      if (!Number(tokens.Next(), bytes)) return false;
      // noreply cannot participate in the proxy's request/response queue.
      // Reject it explicitly rather than desynchronizing subsequent replies.
      if (!tokens.Next().empty()) return false;
      if (len > max_size || max_size - len < 2 ||
          bytes > max_size - len - 2)
        return false;
      key_start = line_start_ + size_t(key.data() - line.data());
      key_size = key.size();
      flags_start = line_start_ + size_t(flags.data() - line.data());
      flags_size = flags.size();
      value_start = len;
      value_size = remaining_ = bytes;
      operation = value ? Operation::kGetRespStart : Operation::kSet;
      values_ = value;
      state_ = bytes ? State::kBody : State::kBodyCR;
      return true;
    }
    if (command == "get") {
      size_t keys = 0;
      for (auto key = tokens.Next(); !key.empty(); key = tokens.Next()) {
        if (!Key(key)) return false;
        ++keys;
      }
      if (!keys) return false;
      operation = Operation::kGet;
    } else if (line == "STORED" || line == "NOT_STORED") {
      operation = Operation::kSetResp;
    } else if (line == "END") {
      operation = Operation::kGetRespEnd;
    } else if (line == "quit") {
      operation = Operation::kQuit;
    } else if (line == "version" ||
               (command == "VERSION" && !tokens.Next().empty())) {
      operation = Operation::kVersion;
    } else if (line == "ERROR" || command == "CLIENT_ERROR" ||
               command == "SERVER_ERROR") {
      operation = Operation::kError;
    } else {
      return false;
    }
    state_ = State::kDone;
    return true;
  }
};

}  // namespace memcached_ascii
