#pragma once

#include <cstdint>
#include <iostream>
#include <lite.hpp>
#include <memory>
#include <string>
#include <vector>

struct Header {
  uint8_t magic = 0;
  uint8_t opcode = 0;
  uint16_t key_length = 0;
  uint8_t extras_length = 0;
  uint8_t data_type = 0;
  uint16_t status = 0;
  uint32_t total_body_length = 0;
  uint32_t opaque = 0;
  uint64_t CAS = 0;

  Header() = default;

  enum class Opcode : uint8_t {
    kGet = 0,
    kSet,
    kAdd,
    kReplace,
    kDelete,
    kIncrement,
    kDecrement,
    kQuit,
    kFlush,
    kGetQ,
    kNoOp,
    kVersion,
    kGetK,
    kGetKQ,
    kAppend,
    kPrepend,
    kStat,
    kSetQ,
    kAddQ,
    kReplaceQ,
    kDeleteQ,
    kIncrementQ,
    kDecrementQ,
    kQuitQ,
    kFlushQ,
    kAppendQ,
    kPrependQ,
  };

  enum class Status : uint16_t {
    kNoError = 0x0000,
    kKeyNotFound,
    kKeyExists,
    kValueTooLarge,
    kInvalidArguments,
    kItemNotStored,
    kIncrDecrOnNonNumericValue,
    kUnknownCommand = 0x0081,
    kOutOfMemory
  };

  static std::vector<uint8_t> kNotFound;

  friend std::ostream &operator<<(std::ostream &os, const Header &rhs) {
    os << "Header: " << std::endl;
    os << "\tmagic: " << uint16_t(rhs.magic) << std::endl;
    os << "\topcode: " << uint16_t(rhs.opcode) << std::endl;
    os << "\tkey_length: " << rhs.key_length << std::endl;
    os << "\textras_length: " << uint16_t(rhs.extras_length) << std::endl;
    os << "\tdata_type: " << uint16_t(rhs.data_type) << std::endl;
    os << "\tstatus/reserved: " << rhs.status << std::endl;
    os << "\ttotal_body_length: " << rhs.total_body_length << std::endl;
    os << "\topaque: " << rhs.opaque << std::endl;
    os << "\tCAS: " << rhs.CAS << std::endl;
    return os;
  }
};

struct Packet {
  // size_t length;
  std::shared_ptr<std::vector<uint8_t>> buffer;
  Packet() : buffer(std::make_shared<std::vector<uint8_t>>()) {}
  Packet(std::shared_ptr<std::vector<uint8_t>> buffer) : buffer(buffer) {}

  virtual std::shared_ptr<std::vector<uint8_t>> Serialize() {
    if (!buffer) {  // empty packet (quiet response)
      return std::make_shared<std::vector<uint8_t>>();
    }
    if (buffer->empty()) {
      std::cerr << "Serialize: buffer is empty" << std::endl;
      return nullptr;
    }
    return buffer;
  }

  std::optional<Header::Opcode> GetOpcode() const {
    return magic_enum::enum_cast<Header::Opcode>((*buffer)[1]);
  }

  uint32_t GetOpaque() const;

  uint16_t GetStatus() const;

#define digest_remaining()             \
  if (remaining_len_ <= end - begin) { \
    begin += remaining_len_;           \
    remaining_len_ = 0;                \
  } else {                             \
    remaining_len_ -= end - begin;     \
    begin = end;                       \
  }
#define input (*begin++)

  lite::DeserializeResult Deserialize(uint8_t *&begin, uint8_t *end) {
    uint8_t *const begin_old = begin;
    while (begin != end) {
      switch (state_) {
        case kMagic:
          input;
          state_ = kOpcode;
          break;
        case kOpcode:
          input;
          state_ = kKeyLength;
          remaining_len_ = 2;
          key_length_ = 0;
          break;
        case kKeyLength:
          key_length_ = (key_length_ << 8) + input;
          remaining_len_--;
          if (!remaining_len_) state_ = kExtrasLength;
          break;
        case kExtrasLength:
          extras_length_ = input;
          state_ = kDataType;
          break;
        case kDataType:
          input;
          state_ = kReserved;
          remaining_len_ = 2;
          break;
        case kReserved:
          digest_remaining();
          if (!remaining_len_) {
            state_ = kTotalBodyLength;
            remaining_len_ = 4;
            total_body_length_ = 0;
          }
          break;
        case kTotalBodyLength:
          total_body_length_ = (total_body_length_ << 8) + input;
          remaining_len_--;
          if (!remaining_len_) {
            state_ = kOpaqueAndCAS;
            remaining_len_ = 12;
          }
          break;
        case kOpaqueAndCAS:
          digest_remaining();
          if (!remaining_len_) {
            if (extras_length_ == 0) goto extra_finished;
            state_ = kExtras;
            remaining_len_ = extras_length_;
          }
          break;
        case kExtras:
          digest_remaining();
          if (!remaining_len_) {
          extra_finished:
            if (key_length_ == 0) goto key_finished;
            state_ = kKey;
            remaining_len_ = key_length_;
          }
          break;
        case kKey:
          digest_remaining();
          if (!remaining_len_) {
          key_finished:
            state_ = kValue;
            remaining_len_ = total_body_length_ - extras_length_ - key_length_;
            if (remaining_len_ == 0) {
              state_ = kMagic;
              buffer->insert(buffer->end(), begin_old, begin);
              return lite::kGood;
            }
          }
          break;
        case kValue:
          digest_remaining();
          if (!remaining_len_) {
            state_ = kMagic;
            buffer->insert(buffer->end(), begin_old, begin);
            return lite::kGood;
          }
          break;
        default:
          return lite::kBad;
      }
    }
    buffer->insert(buffer->end(), begin_old, begin);
    return lite::kIndeterminate;
  }

#undef input
#undef digest_remaining

 private:
  enum state {
    kMagic,
    kOpcode,
    kKeyLength,
    kExtrasLength,
    kDataType,
    kReserved,
    kTotalBodyLength,
    kOpaqueAndCAS,
    kExtras,
    kKey,
    kValue,
  } state_ = kMagic;

  uint32_t remaining_len_;

  uint16_t key_length_;
  uint8_t extras_length_;
  uint32_t total_body_length_;
};

struct ParsedPacket : public Packet {
  Header header = {};
  std::shared_ptr<std::vector<uint8_t>> extra = {};
  std::shared_ptr<std::vector<uint8_t>> key = {};
  std::shared_ptr<std::vector<uint8_t>> value = nullptr;

  ParsedPacket() = default;

  ParsedPacket(const Packet &packet);

  std::shared_ptr<std::vector<uint8_t>> Serialize();

  /// Append the response into a vector of buffers.
  void ToBuffers(std::vector<uint8_t> &buffers);

  friend std::ostream &operator<<(std::ostream &os, const ParsedPacket &rhs) {
    os << rhs.header;
    // os << "extra: \n" << ToString(rhs.extra) << std::endl;
    os << "extra: \n";
    for (const auto byte : *rhs.extra)
      os << std::hex << "0x" << uint32_t(byte) << std::dec << " ";
    os << std::endl;
    os << "key: \n" << ToString(rhs.key.get()) << std::endl;
    os << "value: \n" << ToString(rhs.value.get()) << std::endl;
    return os;
  }

 private:
  static std::string ToString(const std::vector<uint8_t> &v) {
    return std::string(v.begin(), v.end());
  }
  static std::string ToString(const std::vector<uint8_t> *v) {
    return std::string(v->begin(), v->end());
  }
};

class SimpleParser {};