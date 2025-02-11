cmake_minimum_required(VERSION 3.15)

# execute_process(
#     COMMAND which clang
#     OUTPUT_VARIABLE CLANG_PATH
#     OUTPUT_STRIP_TRAILING_WHITESPACE
# )
# execute_process(
#     COMMAND which clang++
#     OUTPUT_VARIABLE CLANGPP_PATH
#     OUTPUT_STRIP_TRAILING_WHITESPACE
# )

# set(CMAKE_C_COMPILER "${CLANG_PATH}")
# set(CMAKE_CXX_COMPILER "${CLANGPP_PATH}")

project("redis-lite"
    VERSION 0.1.5
    DESCRIPTION "The lite version of Redis"
)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -g -O3")
# set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -ggdb -O0 -fsanitize=address,undefined")
# set(CMAKE_BUILD_TYPE Debug)

set(Boost_USE_STATIC_LIBS ON)
find_package(Boost 1.83 REQUIRED COMPONENTS
             system thread)
find_package(Libevent REQUIRED)
find_package(glog 0.4.0 REQUIRED)
find_package(PkgConfig REQUIRED)
# sudo apt-get install google-perftools libgoogle-perftools-dev
pkg_check_modules(TCMALLOC REQUIRED libtcmalloc)
# pkg_check_modules(JEMALLOC REQUIRED jemalloc)

add_subdirectory(Lite)

add_executable(${PROJECT_NAME})

file(GLOB ALL_CC_FILES
  "src/*.cc"
)
target_sources(${PROJECT_NAME}
    PRIVATE
        main.cc
        ${ALL_CC_FILES}
)

target_link_libraries(${PROJECT_NAME} PRIVATE Lite)
target_link_libraries(${PROJECT_NAME} PRIVATE libevent::core Boost::system Boost::thread)
target_link_libraries(${PROJECT_NAME} PRIVATE glog::glog)
target_link_libraries(${PROJECT_NAME} PRIVATE ${TCMALLOC_LIBRARIES})
# target_link_libraries(${PROJECT_NAME} PRIVATE ${JEMALLOC_LIBRARIES})

target_include_directories(${PROJECT_NAME} PRIVATE include)