#pragma once

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
    if (::write(fd, &action, sizeof(action)) != sizeof(action)) {
      return false;
    }
    int len = backend_port.length();
    if (::write(fd, &len, sizeof(len)) != sizeof(len)) {
      return false;
    }
    if (::write(fd, backend_port.c_str(), len) != len) {
      return false;
    }
    return true;
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