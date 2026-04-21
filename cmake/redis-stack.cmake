#cmake_minimum_required(VERSION 3.24)

#project(redis_stack LANGUAGES C CXX)

include(FetchContent)
include(ExternalProject)
#include(GNUInstallDirs)

message(STATUS "========== include redis-stack.cmake ==========")

# Keep the default install tree inside the chosen CMake build directory. This
# avoids accidental writes to /usr/local when the caller does not pass a prefix.
if(CMAKE_INSTALL_PREFIX_INITIALIZED_TO_DEFAULT)
  set(CMAKE_INSTALL_PREFIX "install" CACHE PATH "Install prefix relative to the CMake build directory by default" FORCE)
endif()

find_program(MAKE_EXECUTABLE NAMES gmake make REQUIRED)

set(REDIS_BUILD_TLS "yes" CACHE STRING "Redis BUILD_TLS value: yes, no, or module")
set_property(CACHE REDIS_BUILD_TLS PROPERTY STRINGS yes no module)

set(REDIS_USE_SYSTEMD "no" CACHE STRING "Redis USE_SYSTEMD value: yes or no")
set_property(CACHE REDIS_USE_SYSTEMD PROPERTY STRINGS yes no)

set(REDIS_MALLOC "jemalloc" CACHE STRING "Redis allocator: jemalloc, libc, tcmalloc, or tcmalloc_minimal")
set_property(CACHE REDIS_MALLOC PROPERTY STRINGS jemalloc libc tcmalloc tcmalloc_minimal)

set(REDIS_DISABLE_WERRORS "yes" CACHE STRING "Disable -Werror while building Redis modules")
set_property(CACHE REDIS_DISABLE_WERRORS PROPERTY STRINGS yes no)

set(REDIS_OPENSSL_PREFIX "" CACHE PATH "OpenSSL prefix passed to Redis. Empty disables Redis' /usr/local OpenSSL default.")

option(REDIS_CLEAN_TEMP_RUST "Remove the temporary Rust toolchain after Redis is installed" ON)
option(REDIS_GIT_SHALLOW "Use shallow Git clones for Redis and Redis modules" ON)

set(REDIS_TEMP_RUST_RELATIVE_DIR ".redis-build-tools/rust" CACHE STRING "Temporary Rust directory relative to the Redis install prefix")
set(REDIS_PREPARE_STAMP_RELATIVE_PATH "redis-modules-prepared.stamp" CACHE STRING "Module preparation stamp path relative to the CMake build directory")
set(REDIS_BUILD_WRAPPER_RELATIVE_PATH "build_redis-stack_with_temp_rust.sh" CACHE STRING "Build wrapper path relative to this CMake source directory")

# Redis itself still uses its Makefile build. FetchContent is used only to pin
# and materialize source trees; SOURCE_SUBDIR prevents CMake from trying to
# configure projects that do not provide the CMake entry point we want.
FetchContent_Declare(redis_src
  GIT_REPOSITORY https://github.com/redis/redis.git
  GIT_TAG ${Redis_VERSION}
  GIT_SHALLOW ${REDIS_GIT_SHALLOW}
  SOURCE_SUBDIR __redis_no_cmake__
)

FetchContent_Declare(redisbloom_src
  GIT_REPOSITORY https://github.com/redisbloom/redisbloom.git
  GIT_TAG "v${RedisBloom_VERSION}"
  GIT_SHALLOW ${REDIS_GIT_SHALLOW}
  SOURCE_SUBDIR __redis_no_cmake__
)

# redisearch requires libstdc++-static. 
FetchContent_Declare(redisearch_src
  GIT_REPOSITORY https://github.com/redisearch/redisearch.git
  GIT_TAG "v${RediSearch_VERSION}"
  GIT_SHALLOW ${REDIS_GIT_SHALLOW}
  SOURCE_SUBDIR __redis_no_cmake__
)

FetchContent_Declare(redisjson_src
  GIT_REPOSITORY https://github.com/redisjson/redisjson.git
  GIT_TAG "v${RedisJSON_VERSION}"
  GIT_SHALLOW ${REDIS_GIT_SHALLOW}
  SOURCE_SUBDIR __redis_no_cmake__
)

FetchContent_Declare(redistimeseries_src
  GIT_REPOSITORY https://github.com/redistimeseries/redistimeseries.git
  GIT_TAG "v${RedisTimeSeries_VERSION}"
  GIT_SHALLOW ${REDIS_GIT_SHALLOW}
  SOURCE_SUBDIR __redis_no_cmake__
)

FetchContent_MakeAvailable(
  redis_src
  redisbloom_src
  redisearch_src
  redisjson_src
  redistimeseries_src
)

set(REDIS_SOURCE_DIR "${redis_src_SOURCE_DIR}")

# Resolve user-facing relative paths against the build or install root once, and
# pass absolute paths to the Makefile wrapper. The cache variables above remain
# relative-friendly for command-line usage.
if(IS_ABSOLUTE "${CMAKE_INSTALL_PREFIX}")
  set(REDIS_INSTALL_PREFIX "${CMAKE_INSTALL_PREFIX}")
else()
  cmake_path(ABSOLUTE_PATH CMAKE_INSTALL_PREFIX
    BASE_DIRECTORY "${CMAKE_BINARY_DIR}"
    OUTPUT_VARIABLE REDIS_INSTALL_PREFIX
    NORMALIZE
  )
endif()

cmake_path(ABSOLUTE_PATH REDIS_TEMP_RUST_RELATIVE_DIR
  BASE_DIRECTORY "${REDIS_INSTALL_PREFIX}"
  OUTPUT_VARIABLE REDIS_TEMP_RUST_DIR
  NORMALIZE
)
cmake_path(ABSOLUTE_PATH REDIS_PREPARE_STAMP_RELATIVE_PATH
  BASE_DIRECTORY "${CMAKE_BINARY_DIR}"
  OUTPUT_VARIABLE REDIS_PREPARE_STAMP
  NORMALIZE
)
cmake_path(ABSOLUTE_PATH REDIS_BUILD_WRAPPER_RELATIVE_PATH
  BASE_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
  OUTPUT_VARIABLE REDIS_BUILD_WRAPPER
  NORMALIZE
)

# Redis 8 expects module source trees under redis/modules/<module>/src. Keep the
# independent FetchContent clones as the source of truth, then copy them into the
# Redis tree before invoking the upstream Makefile.
add_custom_command(
  OUTPUT "${REDIS_PREPARE_STAMP}"
  DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/patch_redisearch.cmake"
          "${CMAKE_CURRENT_SOURCE_DIR}/patch_redisjson.cmake"

  COMMAND "${CMAKE_COMMAND}" -E rm -rf "${REDIS_SOURCE_DIR}/modules/redisbloom/src"
  COMMAND "${CMAKE_COMMAND}" -E copy_directory
          "${redisbloom_src_SOURCE_DIR}"
          "${REDIS_SOURCE_DIR}/modules/redisbloom/src"
  COMMAND "${CMAKE_COMMAND}" -E touch
          "${REDIS_SOURCE_DIR}/modules/redisbloom/src/.prepared"

  COMMAND "${CMAKE_COMMAND}" -E rm -rf "${REDIS_SOURCE_DIR}/modules/redisearch/src"
  COMMAND "${CMAKE_COMMAND}" -E copy_directory
          "${redisearch_src_SOURCE_DIR}"
          "${REDIS_SOURCE_DIR}/modules/redisearch/src"
  COMMAND "${CMAKE_COMMAND}"
          "-DREDISEARCH_SOURCE_DIR=${REDIS_SOURCE_DIR}/modules/redisearch/src"
          "-DREDISEARCH_MODULE_MAKEFILE=${REDIS_SOURCE_DIR}/modules/redisearch/Makefile"
          -P "${CMAKE_CURRENT_SOURCE_DIR}/patch_redisearch.cmake"
  COMMAND "${CMAKE_COMMAND}" -E touch
          "${REDIS_SOURCE_DIR}/modules/redisearch/src/.prepared"

  COMMAND "${CMAKE_COMMAND}" -E rm -rf "${REDIS_SOURCE_DIR}/modules/redisjson/src"
  COMMAND "${CMAKE_COMMAND}" -E copy_directory
          "${redisjson_src_SOURCE_DIR}"
          "${REDIS_SOURCE_DIR}/modules/redisjson/src"
  COMMAND "${CMAKE_COMMAND}"
          "-DREDISJSON_SOURCE_DIR=${REDIS_SOURCE_DIR}/modules/redisjson/src"
          -P "${CMAKE_CURRENT_SOURCE_DIR}/patch_redisjson.cmake"
  COMMAND "${CMAKE_COMMAND}" -E touch
          "${REDIS_SOURCE_DIR}/modules/redisjson/src/.prepared"

  COMMAND "${CMAKE_COMMAND}" -E rm -rf "${REDIS_SOURCE_DIR}/modules/redistimeseries/src"
  COMMAND "${CMAKE_COMMAND}" -E copy_directory
          "${redistimeseries_src_SOURCE_DIR}"
          "${REDIS_SOURCE_DIR}/modules/redistimeseries/src"
  COMMAND "${CMAKE_COMMAND}" -E touch
          "${REDIS_SOURCE_DIR}/modules/redistimeseries/src/.prepared"

  COMMAND "${CMAKE_COMMAND}" -E touch "${REDIS_PREPARE_STAMP}"
  COMMENT "Preparing vendored Redis module source trees"
  VERBATIM
)

add_custom_target(redis_prepare_modules DEPENDS "${REDIS_PREPARE_STAMP}")

# Build Redis with its upstream Makefile via cmake/build_redis-stack_with_temp_rust.sh.
# The wrapper provides a temporary Rust/LLVM toolchain, avoids /usr/local
# defaults, and installs Redis plus modules into CMAKE_INSTALL_PREFIX.
ExternalProject_Add(redis_build
  SOURCE_DIR "${REDIS_SOURCE_DIR}"
  CONFIGURE_COMMAND ""
  BUILD_IN_SOURCE 1
  BUILD_ALWAYS TRUE
  BUILD_COMMAND
    "${REDIS_BUILD_WRAPPER}"
    "${REDIS_TEMP_RUST_DIR}"
    "${MAKE_EXECUTABLE}"
    "<SOURCE_DIR>"
    "${REDIS_INSTALL_PREFIX}"
    "${CMAKE_INSTALL_FULL_BINDIR}"
    "${CMAKE_INSTALL_FULL_LIBDIR}"
    "${CMAKE_INSTALL_FULL_SYSCONFDIR}"
    "${CMAKE_C_COMPILER}"
    "${CMAKE_CXX_COMPILER}"
    "${REDIS_BUILD_TLS}"
    "${REDIS_USE_SYSTEMD}"
    "${REDIS_MALLOC}"
    "${REDIS_DISABLE_WERRORS}"
    "${REDIS_OPENSSL_PREFIX}"
    "$<IF:$<BOOL:${REDIS_CLEAN_TEMP_RUST}>,yes,no>"
  INSTALL_COMMAND ""
  BUILD_BYPRODUCTS
    "${CMAKE_INSTALL_BINDIR}/redis-server"
    "${CMAKE_INSTALL_BINDIR}/redis-cli"
    "${CMAKE_INSTALL_BINDIR}/redis-benchmark"
    "${CMAKE_INSTALL_LIBDIR}/redis/modules/redisbloom.so"
    "${CMAKE_INSTALL_LIBDIR}/redis/modules/redisearch.so"
    "${CMAKE_INSTALL_LIBDIR}/redis/modules/rejson.so"
    "${CMAKE_INSTALL_LIBDIR}/redis/modules/redistimeseries.so"
)

add_dependencies(redis_build redis_prepare_modules)

message(STATUS "========== include redis-stack.cmake done ==========")