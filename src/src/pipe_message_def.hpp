#pragma once

#include <stdint.h>

#include <string>

#include "magic_enum.hpp"

namespace lite {

using pipe_message_t = uint64_t;

enum class PipeMessage : pipe_message_t {
  kExitEmergencyMode,
  kEnterEmergencyMode,
};

}  // namespace lite