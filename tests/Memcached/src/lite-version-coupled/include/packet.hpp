#pragma once

#include <iostream>
#include <vector>
#include <memory>

#include "header.hpp"

namespace memcached {
namespace server {

/// A request received from / sent to a client.
struct Packet {
  Header header = {};
  std::vector<uint8_t> extra = {};
  std::vector<uint8_t> key = {};
  std::shared_ptr<std::vector<uint8_t>> value = nullptr;

  Packet() = default;

  /// Append the response into a vector of buffers.
  void ToBuffers(std::vector<uint8_t> &buffers);

  friend std::ostream &operator<<(std::ostream &os, const Packet &rhs) {
    os << rhs.header;
    // os << "extra: \n" << ToString(rhs.extra) << std::endl;
    os << "extra: \n";
    for (const auto byte: rhs.extra)
      os << std::hex << "0x" << uint32_t(byte)  << std::dec << " ";
    os << std::endl;
    os << "key: \n" << ToString(rhs.key) << std::endl;
    os << "value: \n" << ToString(rhs.value.get()) << std::endl;
    return os;
  }

  enum Status {
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

 private:
  static std::string ToString(const std::vector<uint8_t> &v) {
    return std::string(v.begin(), v.end());
  }
  static std::string ToString(const std::vector<uint8_t> *v) {
    return std::string(v->begin(), v->end());
  }
};

}  // namespace server
}  // namespace memcached
