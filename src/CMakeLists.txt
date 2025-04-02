# https://github.com/retifrav/cmake-library-example
cmake_minimum_required(VERSION 3.1...3.27)

set(namespace lite)
project("Lite"
  VERSION 0.2.0
  DESCRIPTION "The library for the lite version framework"
)

# if (NOT CMAKE_PROJECT_NAME STREQUAL PROJECT_NAME)
#   message(STATUS "This project has a top-level one called [${CMAKE_PROJECT_NAME}]")
# else()
#   message(STATUS "This project is a top-level one")
# endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
  set(CMAKE_CXX_FLAGS  "${CMAKE_CXX_FLAGS} -fconcepts-diagnostics-depth=10")
endif()

add_library(${PROJECT_NAME} STATIC)

file(GLOB_RECURSE ALL_CC_FILES
  "src/*.cc"
)

list(FILTER ALL_CC_FILES EXCLUDE REGEX "src/cli.cc")
# message(STATUS "ALL_CC_FILES: ${ALL_CC_FILES}")

target_sources(${PROJECT_NAME}
  PRIVATE
    ${ALL_CC_FILES}
)

target_include_directories(${PROJECT_NAME}
  PRIVATE
    # where the library itself will look for its internal headers
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    # Add binary dir for generated skeleton header
    ${CMAKE_CURRENT_BINARY_DIR}
    ${LIBBPF_INCLUDE_DIRS}
  PUBLIC
    # where top-level project will look for the library's public headers
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    # Add binary dir for generated skeleton header
    $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}>
    # where external projects will look for the library's public headers
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)

# totally optional listing of include directories
get_property(inclds
  TARGET ${PROJECT_NAME}
  PROPERTY INCLUDE_DIRECTORIES
)
message(STATUS "Listing include directories for ${PROJECT_NAME}...")
# "dumb" listing with "raw" generator expressions on configuration
foreach(pth ${inclds})
  message(STATUS "  ${pth}")
endforeach()
# actually evaluated generator expressions printed to file on generation
string(JOIN "\n" includeDirectories ${inclds})
file(GENERATE
  OUTPUT "${CMAKE_BINARY_DIR}/include-directories.txt"
  CONTENT ${includeDirectories}
)

# without it public headers won't get installed
file(GLOB ALL_PUBLIC_HEADERS
  "include/*.hpp"
)
set(public_headers
  ${ALL_PUBLIC_HEADERS}
)

# where to find our CMake modules
set(CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
#message(STATUS "CMake module path: ${CMAKE_MODULE_PATH}")
include(Installing)


# CLI tool
add_executable(lite_cli src/cli.cc)
target_include_directories(lite_cli
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_BINARY_DIR}
)
target_link_libraries(lite_cli PRIVATE ${PROJECT_NAME})

# Add BPF compilation support
find_program(CLANG clang)
if(NOT CLANG)
    message(FATAL_ERROR "Clang not found, required for BPF compilation")
endif()

# Get architecture
execute_process(
    COMMAND uname -m
    OUTPUT_VARIABLE ARCH
    OUTPUT_STRIP_TRAILING_WHITESPACE
)
string(REPLACE "x86_64" "x86" ARCH ${ARCH})
string(REPLACE "aarch64" "arm64" ARCH ${ARCH})

# BPF compilation flags
set(BPF_CFLAGS
    -g
    -O2
    -target bpf
    -D__TARGET_ARCH_${ARCH}
    -I${CMAKE_CURRENT_BINARY_DIR}
    # -I${CMAKE_CURRENT_SOURCE_DIR}/include
    # -fno-stack-protector
    # -fno-jump-tables
    # -fno-common
    # -fno-builtin
    # -mcpu=v3
    # # Add these new flags to handle rodata properly
    # -Xclang -target-feature
    # -Xclang +alu32
    # -D__BPF_TRACING__
)

# Find bpftool
find_program(BPFTOOL bpftool)
if(NOT BPFTOOL)
    message(FATAL_ERROR "bpftool not found, required for BPF skeleton generation")
endif()

# Add custom command to generate vmlinux.h from BTF
add_custom_command(
    OUTPUT
        ${CMAKE_CURRENT_BINARY_DIR}/vmlinux.h
    COMMAND bpftool btf dump file /sys/kernel/btf/vmlinux format c > ${CMAKE_CURRENT_BINARY_DIR}/vmlinux.h
    COMMENT "Generating vmlinux.h from BTF"
)

# Add custom target for vmlinux.h
add_custom_target(generate_vmlinux_header ALL
    DEPENDS
        ${CMAKE_CURRENT_BINARY_DIR}/vmlinux.h
)

# Add custom command to compile BPF program and generate skeleton
add_custom_command(
    OUTPUT
        ${CMAKE_CURRENT_BINARY_DIR}/litesys.bpf.o
        ${CMAKE_CURRENT_BINARY_DIR}/litesys.skel.h
    COMMAND ${CLANG} ${BPF_CFLAGS} -c ${CMAKE_CURRENT_SOURCE_DIR}/include/litesys.bpf.c -o ${CMAKE_CURRENT_BINARY_DIR}/litesys.bpf.o
    COMMAND ${BPFTOOL} gen skeleton ${CMAKE_CURRENT_BINARY_DIR}/litesys.bpf.o > ${CMAKE_CURRENT_BINARY_DIR}/litesys.skel.h
    DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/include/litesys.bpf.c
    COMMENT "Compiling BPF program and generating skeleton"
)

# Add custom target for BPF program and skeleton
add_custom_target(bpf_objects ALL
    DEPENDS
        ${CMAKE_CURRENT_BINARY_DIR}/litesys.bpf.o
        ${CMAKE_CURRENT_BINARY_DIR}/litesys.skel.h
)

# Make sure BPF objects are built before the main target
add_dependencies(${PROJECT_NAME} bpf_objects)

# Make sure vmlinux.h is generated before the bpf_objects target
add_dependencies(bpf_objects generate_vmlinux_header)


# Add libbpf library to the main target
target_link_libraries(${PROJECT_NAME}
    PRIVATE
        ${LIBBPF_LIBRARY}
)

# Install BPF object file and skeleton header
install(FILES
    ${CMAKE_CURRENT_BINARY_DIR}/litesys.bpf.o
    ${CMAKE_CURRENT_BINARY_DIR}/litesys.skel.h
    DESTINATION ${CMAKE_INSTALL_LIBDIR}
)
