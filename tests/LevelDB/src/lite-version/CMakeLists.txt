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

# Use pkg-config to find libbpf
find_package(PkgConfig REQUIRED)
set(ENV{PKG_CONFIG_PATH} "/usr/lib64/pkgconfig:$ENV{PKG_CONFIG_PATH}")
pkg_check_modules(LIBBPF REQUIRED libbpf)

if(NOT LIBBPF_FOUND)
    message(FATAL_ERROR "libbpf not found via pkg-config")
endif()

# Force using the specific libbpf from /usr/lib64
set(LIBBPF_LIBRARY "/usr/lib64/libbpf.so.1")
if(NOT EXISTS ${LIBBPF_LIBRARY})
    message(FATAL_ERROR "Cannot find libbpf at ${LIBBPF_LIBRARY}")
endif()

# Set RPATH
set(CMAKE_INSTALL_RPATH "/usr/lib64")
set(CMAKE_BUILD_WITH_INSTALL_RPATH TRUE)
set(CMAKE_INSTALL_RPATH_USE_LINK_PATH TRUE)

message(STATUS "Using libbpf from: ${LIBBPF_LIBRARY}")

# Remove any potential default paths
set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -Wl,--no-as-needed -Wl,-rpath,/usr/lib64")

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
# Use the absolute path to libbpf
target_link_libraries(${PROJECT_NAME} PRIVATE ${LIBBPF_LIBRARY})

target_include_directories(${PROJECT_NAME}
    PRIVATE
        include
        ${CMAKE_CURRENT_SOURCE_DIR}/Lite/include
)
