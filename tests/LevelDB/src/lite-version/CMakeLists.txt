cmake_minimum_required(VERSION 3.15)

project("LiteLevelDB"
    VERSION 0.9.1
    DESCRIPTION "The lite version of LevelDB"
)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(Boost_USE_STATIC_LIBS ON)
find_package(Boost 1.83 REQUIRED COMPONENTS
             system thread)
find_package(Libevent REQUIRED)
find_package (glog 0.4.0 REQUIRED)

find_library(GPERFTOOLS_LIBRARY NAMES tcmalloc)
if(NOT GPERFTOOLS_LIBRARY)
    message(FATAL_ERROR "gperftools library not found")
endif()

find_library(LIBBPF_LIBRARY NAMES bpf libbpf)
if(NOT LIBBPF_LIBRARY)
    message(FATAL_ERROR "libbpf library not found")
endif()

add_subdirectory(Lite)

add_executable(${PROJECT_NAME})

set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -fsanitize=address,undefined")

file(GLOB ALL_CC_FILES
  "src/*.cc"
)
target_sources(${PROJECT_NAME}
    PRIVATE
        main.cc
        ${ALL_CC_FILES}
)

target_link_libraries(${PROJECT_NAME} PRIVATE Lite)
target_link_libraries(${PROJECT_NAME} PRIVATE libevent::core Boost::system Boost::thread glog::glog tcmalloc)
target_link_libraries(${PROJECT_NAME} PRIVATE ${LIBBPF_LIBRARY})

target_include_directories(${PROJECT_NAME} PRIVATE include)
