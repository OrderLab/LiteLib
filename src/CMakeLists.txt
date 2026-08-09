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
  PUBLIC
    # where top-level project will look for the library's public headers
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
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
)
target_link_libraries(lite_cli PRIVATE ${PROJECT_NAME})
