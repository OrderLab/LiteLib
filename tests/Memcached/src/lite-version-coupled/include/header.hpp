#pragma once

#include <cstdint>
#include <iostream>
#include <string>

namespace memcached {
namespace server {

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

  enum Opcode {
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

}  // namespace server
}  // namespace memcached
