#pragma once

#define GLOG_USE_GLOG_EXPORT 1
#include <glog/logging.h>

#include <chrono>
using namespace std::chrono_literals;

#include <boost/interprocess/allocators/allocator.hpp>
#include <boost/interprocess/managed_shared_memory.hpp>
namespace bip = boost::interprocess;
using SegmentManager = bip::managed_shared_memory::segment_manager;
template <typename T>
using SharedAllocator = bip::allocator<T, SegmentManager>;

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
