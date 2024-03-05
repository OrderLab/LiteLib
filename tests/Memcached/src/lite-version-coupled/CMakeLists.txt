cmake_minimum_required(VERSION 3.1...3.27)

project(
  MemcachedReferenceModel
  VERSION 1.0
  LANGUAGES CXX)

set(Boost_USE_STATIC_LIBS ON)
find_package(Boost 1.83 REQUIRED COMPONENTS
             system thread)
find_package(Libevent REQUIRED)

file(GLOB memcached_reference_model_SRC
  "src/*.cpp"
)

add_executable(memcached_reference_model main.cc ${memcached_reference_model_SRC})
target_link_libraries(memcached_reference_model PRIVATE libevent::core Boost::system Boost::thread)
target_include_directories(memcached_reference_model PRIVATE include)
