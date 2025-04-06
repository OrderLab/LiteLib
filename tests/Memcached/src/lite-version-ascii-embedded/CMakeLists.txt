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

project("LiteMemcached"
    VERSION 1.0.0
    DESCRIPTION "The embedded lite version of Memcached"
)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(Boost_USE_STATIC_LIBS ON)
find_package(Boost 1.87 REQUIRED COMPONENTS
    system thread)
message(STATUS "Found Boost version: ${Boost_VERSION}")
message(STATUS "Boost include dirs: ${Boost_INCLUDE_DIRS}")
message(STATUS "Boost library dirs: ${Boost_LIBRARY_DIRS}")
find_package(Libevent REQUIRED)
find_package(glog 0.4.0 REQUIRED)
find_package(PkgConfig REQUIRED)
# sudo apt-get install google-perftools libgoogle-perftools-dev
pkg_check_modules(TCMALLOC REQUIRED libtcmalloc)
# pkg_check_modules(JEMALLOC REQUIRED jemalloc)

# if(CMAKE_BUILD_TYPE STREQUAL "Debug")
#     set(CMAKE_POSITION_INDEPENDENT_CODE ON)
#     # Ensure sanitizer runtime is linked into shared libraries
#     set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} -fsanitize=address,undefined")
# endif()

add_subdirectory(Lite)

function(link_common_libraries target)
    target_link_libraries(${target} PRIVATE Lite)
    target_link_libraries(${target} PRIVATE libevent::core Boost::system Boost::thread)
    target_link_libraries(${target} PRIVATE glog::glog)
    target_link_libraries(${target} PRIVATE ${TCMALLOC_LIBRARIES})
    # target_link_libraries(${target} PRIVATE ${JEMALLOC_LIBRARIES})
    target_include_directories(${target} PRIVATE include)
endfunction()

# build lite process
add_executable(${PROJECT_NAME})
file(GLOB ALL_CC_FILES "src/*.cc")
target_sources(${PROJECT_NAME}
    PRIVATE
    main.cc
    ${ALL_CC_FILES}
)
link_common_libraries(${PROJECT_NAME})

# build embedded lite library
set(LIBRARY_NAME "embedded_lite_memcached")
file(GLOB LIB_SOURCES "lib/*.cc" "Lite/src/embedded.cc")
add_library(${LIBRARY_NAME} SHARED ${LIB_SOURCES} ${ALL_CC_FILES})
set_target_properties(${LIBRARY_NAME} PROPERTIES
    POSITION_INDEPENDENT_CODE ON
    C_VISIBILITY_PRESET hidden
    CXX_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN ON
)
link_common_libraries(${LIBRARY_NAME})

message(WARNING "After building, run: export LD_LIBRARY_PATH=`pwd`:$LD_LIBRARY_PATH")
