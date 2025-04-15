#pragma once

#define GLOG_USE_GLOG_EXPORT 1
#include <glog/logging.h>

#include <chrono>
using namespace std::chrono_literals;

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
#include "ebpf_worker_impl.hpp"

#include "embedded_lite_impl.hpp"