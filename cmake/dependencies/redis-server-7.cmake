include(FetchContent)
include(ExternalProject)

message(STATUS "========== include redis-server-7.cmake ==========")

# Keep the default install tree inside the chosen CMake build directory. This
# avoids accidental writes to /usr/local when the caller does not pass a prefix.
if(CMAKE_INSTALL_PREFIX_INITIALIZED_TO_DEFAULT)
  set(CMAKE_INSTALL_PREFIX "install" CACHE PATH "Install prefix relative to the CMake build directory by default" FORCE)
endif()

find_program(MAKE_EXECUTABLE NAMES gmake make REQUIRED)

set(REDIS_SERVER_7_SERIES "7.4" CACHE STRING "Redis 7.x series: 7.4 or 7.2")
set_property(CACHE REDIS_SERVER_7_SERIES PROPERTY STRINGS 7.4 7.2)

if(REDIS_SERVER_7_SERIES STREQUAL "7.4")
  set(NESTDAQ_DEFAULT_REDIS7_VERSION "7.4.11")
  set(NESTDAQ_DEFAULT_REDISTIMESERIES7_VERSION "1.12.14")
elseif(REDIS_SERVER_7_SERIES STREQUAL "7.2")
  set(NESTDAQ_DEFAULT_REDIS7_VERSION "7.2.16")
  set(NESTDAQ_DEFAULT_REDISTIMESERIES7_VERSION "1.10.24")
else()
  message(FATAL_ERROR "Unsupported REDIS_SERVER_7_SERIES='${REDIS_SERVER_7_SERIES}'. Use 7.4 or 7.2.")
endif()

set(Redis7_VERSION "${NESTDAQ_DEFAULT_REDIS7_VERSION}" CACHE STRING "Redis 7.x version tag")
set(RedisTimeSeries7_VERSION "${NESTDAQ_DEFAULT_REDISTIMESERIES7_VERSION}" CACHE STRING "RedisTimeSeries standalone version tag for Redis 7.x")
message(STATUS "REDIS_SERVER_7_SERIES selected: ${REDIS_SERVER_7_SERIES}")
message(STATUS "Redis7_VERSION selected: ${Redis7_VERSION}")
message(STATUS "RedisTimeSeries7_VERSION selected: ${RedisTimeSeries7_VERSION}")

set(REDIS7_BUILD_TLS "yes" CACHE STRING "Redis BUILD_TLS value: yes, no, or module")
set_property(CACHE REDIS7_BUILD_TLS PROPERTY STRINGS yes no module)

set(REDIS7_USE_SYSTEMD "no" CACHE STRING "Redis USE_SYSTEMD value: yes or no")
set_property(CACHE REDIS7_USE_SYSTEMD PROPERTY STRINGS yes no)

set(REDIS7_MALLOC "jemalloc" CACHE STRING "Redis allocator: jemalloc, libc, tcmalloc, or tcmalloc_minimal")
set_property(CACHE REDIS7_MALLOC PROPERTY STRINGS jemalloc libc tcmalloc tcmalloc_minimal)

set(REDIS7_OPENSSL_PREFIX "" CACHE PATH "OpenSSL prefix passed to Redis. Empty disables Redis' /usr/local OpenSSL default.")
option(REDIS7_GIT_SHALLOW "Use shallow Git clones for Redis 7 and RedisTimeSeries" ON)

set(REDIS7_BUILD_WRAPPER_RELATIVE_PATH "build_redis-server-7_with_redistimeseries.sh" CACHE STRING "Build wrapper path relative to this CMake file directory")

# Redis and RedisTimeSeries still use Makefile builds. FetchContent is used
# only to pin and materialize source trees. RedisTimeSeries is built as a
# standalone module for Redis 7.x; it is not copied into redis/modules as the
# Redis 8 Stack build does.
FetchContent_Declare(redis7_src
  GIT_REPOSITORY https://github.com/redis/redis.git
  GIT_TAG ${Redis7_VERSION}
  GIT_SHALLOW ${REDIS7_GIT_SHALLOW}
  SOURCE_SUBDIR __redis_no_cmake__
)

FetchContent_Declare(redistimeseries7_src
  GIT_REPOSITORY https://github.com/RedisTimeSeries/RedisTimeSeries.git
  GIT_TAG "v${RedisTimeSeries7_VERSION}"
  GIT_SHALLOW ${REDIS7_GIT_SHALLOW}
  GIT_SUBMODULES_RECURSE TRUE
  SOURCE_SUBDIR __redis_no_cmake__
)

FetchContent_MakeAvailable(redis7_src redistimeseries7_src)

if(IS_ABSOLUTE "${CMAKE_INSTALL_PREFIX}")
  set(REDIS7_INSTALL_PREFIX "${CMAKE_INSTALL_PREFIX}")
else()
  cmake_path(ABSOLUTE_PATH CMAKE_INSTALL_PREFIX
    BASE_DIRECTORY "${CMAKE_BINARY_DIR}"
    OUTPUT_VARIABLE REDIS7_INSTALL_PREFIX
    NORMALIZE
  )
endif()

cmake_path(ABSOLUTE_PATH REDIS7_BUILD_WRAPPER_RELATIVE_PATH
  BASE_DIRECTORY "${CMAKE_CURRENT_LIST_DIR}"
  OUTPUT_VARIABLE REDIS7_BUILD_WRAPPER
  NORMALIZE
)

set(REDIS7_BUILD_BYPRODUCTS
  "${CMAKE_INSTALL_FULL_BINDIR}/redis-server"
  "${CMAKE_INSTALL_FULL_BINDIR}/redis-cli"
  "${CMAKE_INSTALL_FULL_BINDIR}/redis-benchmark"
  "${CMAKE_INSTALL_FULL_LIBDIR}/redis/modules/redistimeseries.so"
  "${CMAKE_INSTALL_FULL_SYSCONFDIR}/redis/redis.conf"
  "${CMAKE_INSTALL_FULL_SYSCONFDIR}/redis/redis-full.conf"
)

ExternalProject_Add(redis7_build
  SOURCE_DIR "${redis7_src_SOURCE_DIR}"
  CONFIGURE_COMMAND ""
  BUILD_IN_SOURCE 1
  BUILD_ALWAYS TRUE
  BUILD_COMMAND
    "${REDIS7_BUILD_WRAPPER}"
    "${MAKE_EXECUTABLE}"
    "<SOURCE_DIR>"
    "${redistimeseries7_src_SOURCE_DIR}"
    "${REDIS7_INSTALL_PREFIX}"
    "${CMAKE_INSTALL_FULL_BINDIR}"
    "${CMAKE_INSTALL_FULL_LIBDIR}"
    "${CMAKE_INSTALL_FULL_SYSCONFDIR}"
    "${CMAKE_C_COMPILER}"
    "${CMAKE_CXX_COMPILER}"
    "${REDIS7_BUILD_TLS}"
    "${REDIS7_USE_SYSTEMD}"
    "${REDIS7_MALLOC}"
    "${REDIS7_OPENSSL_PREFIX}"
  INSTALL_COMMAND ""
  BUILD_BYPRODUCTS ${REDIS7_BUILD_BYPRODUCTS}
)

message(STATUS "========== include redis-server-7.cmake done ==========")
