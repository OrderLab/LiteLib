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

# Find libbpf
find_path(LIBBPF_INCLUDE_DIR
    NAMES bpf/libbpf.h bpf/bpf.h
    PATHS /usr/include /usr/local/include
)

find_library(LIBBPF_LIBRARY
    NAMES bpf libbpf
    PATHS /usr/lib /usr/local/lib /usr/lib/x86_64-linux-gnu /usr/lib64
)

if(NOT LIBBPF_INCLUDE_DIR OR NOT LIBBPF_LIBRARY)
    message(STATUS "libbpf include dir: ${LIBBPF_INCLUDE_DIR}")
    message(STATUS "libbpf library: ${LIBBPF_LIBRARY}")
    
    # Instead of failing, let's provide instructions
    message(WARNING "libbpf not found. Installing libbpf...")
    
    # You can add commands to install libbpf automatically
    execute_process(
        COMMAND sudo apt-get update
        COMMAND sudo apt-get install -y libbpf-dev
        RESULT_VARIABLE LIBBPF_INSTALL_RESULT
    )
    
    if(NOT LIBBPF_INSTALL_RESULT EQUAL 0)
        message(FATAL_ERROR "Failed to install libbpf. Please install manually with: sudo apt-get install libbpf-dev")
    endif()
    
    # Try finding it again
    find_path(LIBBPF_INCLUDE_DIR
        NAMES bpf/libbpf.h bpf/bpf.h
        PATHS /usr/include /usr/local/include
    )
    
    find_library(LIBBPF_LIBRARY
        NAMES bpf libbpf
        PATHS /usr/lib /usr/local/lib /usr/lib64
    )
    
    if(NOT LIBBPF_INCLUDE_DIR OR NOT LIBBPF_LIBRARY)
        message(FATAL_ERROR "libbpf still not found after installation attempt. Please install manually.")
    endif()
endif()

message(STATUS "Found libbpf: ${LIBBPF_LIBRARY}")
message(STATUS "Found libbpf include: ${LIBBPF_INCLUDE_DIR}")

# Add the include directory
include_directories(${LIBBPF_INCLUDE_DIR})

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
