cmake_minimum_required(VERSION 3.16)
project(LiteCacheTests LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
find_package(Threads REQUIRED)
find_package(glog REQUIRED)
find_package(Boost 1.87 REQUIRED)

option(LITE_CACHE_SANITIZERS "Enable AddressSanitizer and UndefinedBehaviorSanitizer" OFF)
add_executable(cache_concurrency_test cache_concurrency_test.cc ../src/sliding_window.cc)
target_include_directories(cache_concurrency_test PRIVATE ../include ${Boost_INCLUDE_DIRS})
target_link_libraries(cache_concurrency_test PRIVATE Threads::Threads glog::glog)
target_compile_options(cache_concurrency_test PRIVATE -UNDEBUG)
if(LITE_CACHE_SANITIZERS)
  target_compile_options(cache_concurrency_test PRIVATE
    -fsanitize=address,undefined -fno-omit-frame-pointer -fno-pie)
  target_link_options(cache_concurrency_test PRIVATE -fsanitize=address,undefined -no-pie)
endif()

enable_testing()
add_executable(pipe_message_test pipe_message_test.cc)
target_include_directories(pipe_message_test PRIVATE ../include ../src)
target_link_libraries(pipe_message_test PRIVATE Threads::Threads)
target_compile_options(pipe_message_test PRIVATE -UNDEBUG)
if(LITE_CACHE_SANITIZERS)
  target_compile_options(pipe_message_test PRIVATE
    -fsanitize=address,undefined -fno-omit-frame-pointer -fno-pie)
  target_link_options(pipe_message_test PRIVATE -fsanitize=address,undefined -no-pie)
endif()
add_test(NAME control_pipe_atomic_frame COMMAND pipe_message_test atomic)
set_tests_properties(control_pipe_atomic_frame PROPERTIES TIMEOUT 15)
add_test(NAME cache_serial COMMAND cache_concurrency_test serial)
add_test(NAME cache_delete COMMAND cache_concurrency_test delete)
add_test(NAME cache_emergency_same_key COMMAND cache_concurrency_test 8 20000 1 1)
add_test(NAME cache_emergency_many_keys COMMAND cache_concurrency_test 8 10000 1 64)
add_test(NAME cache_normal_same_key COMMAND cache_concurrency_test 8 10000 0 1)
add_test(NAME cache_normal_many_keys COMMAND cache_concurrency_test 8 10000 0 64)
set_tests_properties(cache_serial cache_delete cache_emergency_same_key
  cache_emergency_many_keys cache_normal_same_key cache_normal_many_keys
  PROPERTIES TIMEOUT 90)

set(MEMCACHED_ASCII_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../../tests/Memcached/src)
foreach(variant IN ITEMS proxy ebpf embedded)
  add_executable(ascii_${variant}_test ascii_framing_test.cc)
  if(variant STREQUAL "embedded")
    target_compile_definitions(ascii_${variant}_test PRIVATE ASCII_EMBEDDED)
    target_include_directories(ascii_${variant}_test PRIVATE
      ../../src_embedded/include
      ${MEMCACHED_ASCII_DIR}/lite-version-ascii-embedded/include)
    target_sources(ascii_${variant}_test PRIVATE
      ${MEMCACHED_ASCII_DIR}/lite-version-ascii-embedded/src/service.cc
      ../../src_embedded/src/sliding_window.cc)
  else()
    if(variant STREQUAL "proxy")
      set(adapter lite-version-ascii)
    else()
      set(adapter lite-version-ascii-ebpf)
    endif()
    target_include_directories(ascii_${variant}_test PRIVATE
      ../include ${MEMCACHED_ASCII_DIR}/${adapter}/include)
    target_sources(ascii_${variant}_test PRIVATE
      ${MEMCACHED_ASCII_DIR}/${adapter}/src/service.cc ../src/sliding_window.cc)
  endif()
  add_test(NAME ascii_${variant}_adapter COMMAND ascii_${variant}_test adapter)
  set_tests_properties(ascii_${variant}_adapter PROPERTIES TIMEOUT 30)
  target_include_directories(ascii_${variant}_test PRIVATE ${Boost_INCLUDE_DIRS})
  target_link_libraries(ascii_${variant}_test PRIVATE Threads::Threads glog::glog event)
  target_compile_options(ascii_${variant}_test PRIVATE -UNDEBUG)
  if(LITE_CACHE_SANITIZERS)
    target_compile_options(ascii_${variant}_test PRIVATE
      -fsanitize=address,undefined -fno-omit-frame-pointer -fno-pie)
    target_link_options(ascii_${variant}_test PRIVATE
      -fsanitize=address,undefined -no-pie)
  endif()
  foreach(test IN ITEMS parser randomized)
    add_test(NAME ascii_${variant}_${test} COMMAND ascii_${variant}_test ${test})
    set_tests_properties(ascii_${variant}_${test} PROPERTIES TIMEOUT 30)
  endforeach()
endforeach()
