#pragma once

#include <stdint.h>

#include <string>

#include "magic_enum.hpp"

namespace lite {

enum class PipeMessage : uint8_t {
  kExitEmergencyMode,
  kEnterEmergencyMode,
};

struct pipe_message_t {
  PipeMessage action;
  uint16_t backend_port;
};

}  // namespace lite