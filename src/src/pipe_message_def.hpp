#pragma once

#include <stdint.h>
#include <errno.h>
#include <limits.h>
#include <unistd.h>

#include <cstring>
#include <string>
#include <vector>

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
    constexpr size_t header_size = sizeof(action) + sizeof(int);
    if (backend_port.size() > PIPE_BUF - header_size) {
      errno = EMSGSIZE;
      return false;
    }
    const int len = static_cast<int>(backend_port.size());
    std::vector<char> frame(header_size + backend_port.size());
    std::memcpy(frame.data(), &action, sizeof(action));
    std::memcpy(frame.data() + sizeof(action), &len, sizeof(len));
    std::memcpy(frame.data() + header_size, backend_port.data(), backend_port.size());
    // A nonblocking reader must not observe the action before its payload.
    ssize_t written;
    do {
      written = ::write(fd, frame.data(), frame.size());
    } while (written == -1 && errno == EINTR);
    return written == static_cast<ssize_t>(frame.size());
  }

  bool read(int fd) {
    if (::read(fd, &action, sizeof(action)) != sizeof(action)) {
      return false;
    }
    int len;
    if (::read(fd, &len, sizeof(len)) != sizeof(len)) {
      return false;
    }
    backend_port.resize(len);
    if (::read(fd, backend_port.data(), len) != len) {
      return false;
    }
    return true;
  }
};

}  // namespace lite