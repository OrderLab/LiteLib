#pragma once

#define GLOG_USE_GLOG_EXPORT 1
#include <glog/logging.h>

#include <chrono>
using namespace std::chrono_literals;

struct ProfileRecord {
  decltype(std::chrono::system_clock::now()) timestamp;
  std::string message;

  ProfileRecord(decltype(timestamp) timestamp, std::string message)
      : timestamp(timestamp), message(message) {}
};
extern std::vector<ProfileRecord> profiles;

#include "cache_impl.hpp"
#include "cache_inner_impl.hpp"
#include "concept.hpp"
#include "connection_impl.hpp"
#include "core_impl.hpp"
#include "logger_impl.hpp"
#include "logger_inner_impl.hpp"
#include "magic_enum.hpp"
#include "server_impl.hpp"
#include "worker_impl.hpp"
