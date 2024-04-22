#pragma once

#include <iostream>

#include "concept.hpp"
#include "packet.hpp"

class RESPIntegerParser : public RESPTypeParser {
  bool is_positive_ = true;
  enum State { kSign, kCR, kLF } state_ = kSign;

 public:
  lite::DeserializeResult Deserialize(InputIterator &begin, InputIterator end,
                                      RESPType &value) override {
    RESPInteger &typed_value = dynamic_cast<RESPInteger &>(value);
    switch (state_) {
      case kSign: {
        if (begin == end) return lite::kIndeterminate;
        if (*begin == '-') {
          is_positive_ = false;
          ++begin;
        } else if (*begin == '+') {
          ++begin;
        }
        state_ = kCR;
      }
      case kCR: {
        if (begin == end) return lite::kIndeterminate;
        while (begin != end && *begin != '\r') {
          typed_value.value = typed_value.value * 10 + (*(begin++) - '0');
        }
        if (*begin == '\r') {
          state_ = kLF;
          ++begin;
        }
      }
      case kLF: {
        if (begin == end) return lite::kIndeterminate;
        if (*(begin++) != '\n') {
          std::cerr << "Could not parse integer: no \\n" << std::endl;
          return lite::kBad;
        }
        typed_value.value = typed_value.value * (is_positive_ ? 1 : -1);
        return lite::kGood;
      }
    }
  }
};

class RESPSimpleStringParser : public RESPTypeParser {
  enum State { kCR, kLF } state_ = kCR;

 public:
  lite::DeserializeResult Deserialize(InputIterator &begin, InputIterator end,
                                      RESPType &value) override {
    RESPString &typed_value = dynamic_cast<RESPString &>(value);
    switch (state_) {
      case kCR: {
        auto start = begin;
        while (begin != end && *begin != '\r') ++begin;
        typed_value.value->insert(typed_value.value->end(), start, begin);
        if (*begin == '\r') {
          state_ = kLF;
          ++begin;
        }
      }
      case kLF: {
        if (begin == end) return lite::kIndeterminate;
        if (*(begin++) != '\n') {
          std::cerr << "Could not parse string: no \\n" << std::endl;
          return lite::kBad;
        }
        return lite::kGood;
      }
    }
  }
};

class RESPBulkStringParser : public RESPTypeParser {
  enum State { kLength, kData, kCR, kLF } state_ = kLength;
  RESPIntegerParser length_parser_;
  RESPInteger length_;

 public:
  lite::DeserializeResult Deserialize(InputIterator &begin, InputIterator end,
                                      RESPType &value) override {
    RESPString &typed_value = dynamic_cast<RESPBulkString &>(value);
    switch (state_) {
      case kLength: {
        const auto result = length_parser_.Deserialize(begin, end, length_);
        if (result == lite::kGood) {
          if (length_.value == -1) {
            typed_value.value = nullptr;
            return lite::kGood;
          }
          typed_value.value->reserve(length_.value);
          state_ = kData;
        } else {
          return result;
        }
      }
      case kData: {
        if (begin + length_.value <= end) {
          typed_value.value->insert(typed_value.value->end(), begin,
                                    begin + length_.value);
          begin += length_.value;
          state_ = kCR;
        } else {
          typed_value.value->insert(typed_value.value->end(), begin, end);
          length_.value -= end - begin;
          begin = end;
          return lite::kIndeterminate;
        }
      }
      case kCR: {
        if (begin == end) return lite::kIndeterminate;
        if (*(begin++) != '\r') {
          std::cerr << "Could not parse bulk string: no \\r" << std::endl;
          return lite::kBad;
        }
        state_ = kLF;
      }
      case kLF: {
        if (begin == end) return lite::kIndeterminate;
        if (*(begin++) != '\n') {
          std::cerr << "Could not parse bulk string: no \\n" << std::endl;
          return lite::kBad;
        }
        return lite::kGood;
      }
    }
  }
};

class RESPArrayParser : public RESPTypeParser {
  enum State { kLength, kData } state_ = kLength;
  RESPIntegerParser length_parser_;
  RESPInteger length_;
  std::unique_ptr<RESPTypeParser> data_parser_ =
      std::make_unique<RESPTypeParser>();

 public:
  lite::DeserializeResult Deserialize(InputIterator &begin, InputIterator end,
                                      RESPType &value) override {
    RESPArray &typed_value = dynamic_cast<RESPArray &>(value);
    switch (state_) {
      case kLength: {
        const auto result = length_parser_.Deserialize(begin, end, length_);
        if (result == lite::kGood) {
          state_ = kData;
          typed_value.value.reserve(length_.value);
        } else {
          return result;
        }
      }
      case kData: {
        while (begin != end && length_.value) {
          const auto result = data_parser_->Deserialize(begin, end);
          if (result == lite::kGood) {
            length_.value--;
            typed_value.value.emplace_back(std::move(data_parser_->value_));
            data_parser_ = std::make_unique<RESPTypeParser>();
          } else {
            return result;
          }
        }
        if (length_.value) return lite::kIndeterminate;
        return lite::kGood;
      }
    }
  }
};