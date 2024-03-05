#pragma once

#include <cstdint>
#include <iostream>
#include <tuple>

#include "packet.hpp"

namespace memcached {
namespace server {

/// Parser for incoming requests.
class RequestParser {
 public:
  /// Construct ready to parse the request method.
  RequestParser() : state_(kMagic) {}

  /// Reset to initial parser state.
  void Reset() { state_ = kMagic; }

  /// Result of parse.
  enum ResultType { kGood, kBad, kIndeterminate };

  /// Parse some data. The enum return value is good when a complete request has
  /// been parsed, bad if the data is invalid, indeterminate when more data is
  /// required. The InputIterator return value indicates how much of the input
  /// has been consumed.
  template <typename InputIterator>
  ResultType Parse(Packet& req, InputIterator& begin, InputIterator end) {
#define input (*begin++)
    while (begin != end) {
      switch (state_) {
        case kMagic:
          req.header.magic = input;
          state_ = kOpcode;
          break;
        case kOpcode:
          req.header.opcode = input;
          state_ = kKeyLength;
          remaining_len_ = 2;
          req.header.key_length = 0;
          break;
        case kKeyLength:
          req.header.key_length = (req.header.key_length << 8) + input;
          remaining_len_--;
          if (!remaining_len_) state_ = kExtrasLength;
          break;
        case kExtrasLength:
          req.header.extras_length = input;
          state_ = kDataType;
          break;
        case kDataType:
          req.header.data_type = input;
          state_ = kReserved;
          remaining_len_ = 2;
          req.header.status = 0;
          break;
        case kReserved:
          req.header.status = (req.header.status << 8) + input;
          remaining_len_--;
          if (!remaining_len_) {
            state_ = kTotalBodyLength;
            remaining_len_ = 4;
            req.header.total_body_length = 0;
          }
          break;
        case kTotalBodyLength:
          req.header.total_body_length =
              (req.header.total_body_length << 8) + input;
          remaining_len_--;
          if (!remaining_len_) {
            state_ = kOpaque;
            remaining_len_ = 4;
            req.header.opaque = 0;
          }
          break;
        case kOpaque:
          req.header.opaque = (req.header.opaque << 8) + input;
          remaining_len_--;
          if (!remaining_len_) {
            state_ = kCAS;
            remaining_len_ = 8;
            req.header.CAS = 0;
          }
          break;
        case kCAS:
          req.header.CAS = (req.header.CAS << 8) + input;
          remaining_len_--;
          if (!remaining_len_) {
            if (req.header.extras_length == 0) goto extra_finished;
            state_ = kExtras;
            remaining_len_ = req.header.extras_length;
            req.extra.reserve(remaining_len_);
            req.extra.clear();
          }
          break;
        case kExtras:
          if (remaining_len_ <= end - begin) {
            req.extra.insert(req.extra.end(), begin, begin + remaining_len_);
            begin += remaining_len_;
            remaining_len_ = 0;
          } else {
            req.extra.insert(req.extra.end(), begin, end);
            remaining_len_ -= end - begin;
            begin = end;
          }
          if (!remaining_len_) {
          extra_finished:
            if (req.header.key_length == 0) goto key_finished;
            state_ = kKey;
            remaining_len_ = req.header.key_length;
            req.key.reserve(remaining_len_);
            req.key.clear();
          }
          break;
        case kKey:
          if (remaining_len_ <= end - begin) {
            req.key.insert(req.key.end(), begin, begin + remaining_len_);
            begin += remaining_len_;
            remaining_len_ = 0;
          } else {
            req.key.insert(req.key.end(), begin, end);
            remaining_len_ -= end - begin;
            begin = end;
          }
          if (!remaining_len_) {
          key_finished:
            state_ = kValue;
            remaining_len_ = req.header.total_body_length -
                             req.header.extras_length - req.header.key_length;
            req.value->clear();
            req.value->reserve(remaining_len_);
            if (remaining_len_ == 0) {
              state_ = kMagic;
              return kGood;
            }
          }
          break;
        case kValue:
          if (remaining_len_ <= end - begin) {
            req.value->insert(req.value->end(), begin, begin + remaining_len_);
            begin += remaining_len_;
            remaining_len_ = 0;
          } else {
            req.value->insert(req.value->end(), begin, end);
            remaining_len_ -= end - begin;
            begin = end;
          }
          if (!remaining_len_) {
            state_ = kMagic;
            return kGood;
          }
          break;
        default:
          return kBad;
      }
    }
    return kIndeterminate;
#undef input
  }

 private:
  uint32_t remaining_len_;

  /// The current state of the parser.
  enum state {
    kMagic,
    kOpcode,
    kKeyLength,
    kExtrasLength,
    kDataType,
    kReserved,
    kTotalBodyLength,
    kOpaque,
    kCAS,
    kExtras,
    kKey,
    kValue
  } state_;
};

}  // namespace server
}  // namespace memcached
