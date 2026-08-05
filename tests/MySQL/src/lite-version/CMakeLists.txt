cmake_minimum_required(VERSION 3.15)

project("LiteMySQL"
    VERSION 0.9.1
    DESCRIPTION "The lite version of Proxy"
)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(Boost_USE_STATIC_LIBS ON)
set(Boost_USE_MULTITHREADED ON)
set(Boost_USE_STATIC_RUNTIME OFF)

# Find Boost with the components we need
find_package(Boost 1.87.0 REQUIRED COMPONENTS system thread)

find_package(Libevent REQUIRED)
find_package (glog 0.4.0 REQUIRED)

add_subdirectory(Lite)

add_executable(${PROJECT_NAME})

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fno-omit-frame-pointer -g -no-pie -fno-pie")
set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -fsanitize=address,undefined -g -no-pie -fno-pie")
set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -g -no-pie -fno-pie")

# Define macro for Boost.Stacktrace to use addr2line
add_definitions(-DBOOST_STACKTRACE_USE_ADDR2LINE)

file(GLOB ALL_CC_FILES
  "src/*.cc"
  "src/**/*.cc"
)
target_sources(${PROJECT_NAME}
    PRIVATE
        main.cc
        ${ALL_CC_FILES}
)

target_link_libraries(${PROJECT_NAME} PRIVATE Lite)
target_link_libraries(${PROJECT_NAME} PRIVATE libevent::core Boost::system Boost::thread glog::glog sqlparser)

# Manually link stacktrace libraries
target_link_libraries(${PROJECT_NAME} PRIVATE
    boost_stacktrace_basic
    boost_stacktrace_backtrace
    dl
)

target_include_directories(${PROJECT_NAME} PRIVATE include)
