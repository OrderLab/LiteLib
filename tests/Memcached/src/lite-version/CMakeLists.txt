cmake_minimum_required(VERSION 3.15)

project("LiteMemcached"
    VERSION 0.9.1
    DESCRIPTION "The lite version of Memcached"
)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(Boost_USE_STATIC_LIBS ON)
find_package(Boost 1.83 REQUIRED COMPONENTS
             system thread)
find_package(Libevent REQUIRED)
find_package (glog 0.6.0 REQUIRED)

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
target_link_libraries(${PROJECT_NAME} PRIVATE libevent::core Boost::system Boost::thread glog::glog)
target_include_directories(${PROJECT_NAME} PRIVATE include)
