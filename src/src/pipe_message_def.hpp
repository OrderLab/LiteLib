#pragma once

#include <array>
#include <cerrno>
#include <cstring>
#include <limits.h>
#include <stdint.h>

#include <string>

#include "magic_enum.hpp"

namespace lite {

enum class PipeMessage : uint8_t {
  kExitEmergencyMode,
  kEnterEmergencyMode,
};

class pipe_message_t {
 public:
  PipeMessage action;
  std::string backend_port;

  bool write(int fd) {
    int len = backend_port.length();
    std::string buffer(sizeof(action) + sizeof(len) + len, '\0');
    std::memcpy(buffer.data(), &action, sizeof(action));
    std::memcpy(buffer.data() + sizeof(action), &len, sizeof(len));
    std::memcpy(
        buffer.data() + sizeof(action) + sizeof(len),
        backend_port.data(),
        len);
    if (buffer.size() > PIPE_BUF) {
      return false;
    }
    ssize_t written;
    do {
      written = ::write(fd, buffer.data(), buffer.size());
    } while (written == -1 && errno == EINTR);
    return written == static_cast<ssize_t>(buffer.size());
  }

  bool read(int fd) {
    std::array<char, PIPE_BUF> buffer;
    ssize_t count;
    do {
      count = ::read(fd, buffer.data(), buffer.size());
    } while (count == -1 && errno == EINTR);
    if (count < static_cast<ssize_t>(sizeof(action) + sizeof(int))) {
      return false;
    }
    int len;
    std::memcpy(&action, buffer.data(), sizeof(action));
    std::memcpy(&len, buffer.data() + sizeof(action), sizeof(len));
    if (len < 0 ||
        count != static_cast<ssize_t>(sizeof(action) + sizeof(len) + len)) {
      return false;
    }
    backend_port.assign(
        buffer.data() + sizeof(action) + sizeof(len),
        static_cast<size_t>(len));
    return true;
  }
};

}  // namespace lite