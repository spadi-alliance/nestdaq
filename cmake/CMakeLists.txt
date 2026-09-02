cmake_minimum_required(VERSION 3.22)

project(nestdaq_external_dependencies LANGUAGES CXX C)

include("${CMAKE_CURRENT_LIST_DIR}/common.cmake")

#------------------------------------------------------------------------------
set(NESTDAQ_DEPENDENCIES_CMAKE_DIR "${CMAKE_CURRENT_LIST_DIR}/dependencies")

if(BUILD_PARALLEL_LEVEL)
  set(BUILD_PARALLEL_LEVEL "--parallel ${BUILD_PARALLEL_LEVEL}")
endif()

option(WITH_REDIS_STACK "Build and install Redis Stack" ON)
option(WITH_REDIS_SERVER_7 "Build and install Redis 7.x server with standalone RedisTimeSeries" OFF)
if(WITH_REDIS_STACK AND WITH_REDIS_SERVER_7)
  message(FATAL_ERROR "WITH_REDIS_STACK and WITH_REDIS_SERVER_7 are mutually exclusive. Use Redis Stack 8.x or Redis 7.x with standalone RedisTimeSeries, not both.")
endif()

set(ZeroMQ_VERSION "4.3.5" CACHE STRING "ZeroMQ version tag")
include("${NESTDAQ_DEPENDENCIES_CMAKE_DIR}/ZeroMQ.cmake")

set(Boost_VERSION "1.85.0" CACHE STRING "boost version tag")
include("${NESTDAQ_DEPENDENCIES_CMAKE_DIR}/Boost.cmake")

set(FairLogger_VERSION "2.3.0" CACHE STRING "FairLogger version tag")
include("${NESTDAQ_DEPENDENCIES_CMAKE_DIR}/FairLogger.cmake")

set(NESTDAQ_DEFAULT_FAIRMQ_VERSION "1.10.0")
if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU"
    AND CMAKE_CXX_COMPILER_VERSION VERSION_LESS 9.1)
  set(NESTDAQ_DEFAULT_FAIRMQ_VERSION "1.9.2")
endif()
set(FairMQ_VERSION "${NESTDAQ_DEFAULT_FAIRMQ_VERSION}" CACHE STRING "FairMQ version tag")
message(STATUS "FairMQ_VERSION default for ${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}: ${NESTDAQ_DEFAULT_FAIRMQ_VERSION}")
message(STATUS "FairMQ_VERSION selected: ${FairMQ_VERSION}")
include("${NESTDAQ_DEPENDENCIES_CMAKE_DIR}/FairMQ.cmake")

set(Catch2_VERSION "3.15.2" CACHE STRING "Catch2 version tag")
include("${NESTDAQ_DEPENDENCIES_CMAKE_DIR}/Catch2.cmake")

set(nlohmann_json_VERSION "3.12.0" CACHE STRING "nlohmann/json version tag")
include("${NESTDAQ_DEPENDENCIES_CMAKE_DIR}/nlohmann_json.cmake")

option(WITH_SPDLOG "Build and install spdlog" ON)
if(WITH_SPDLOG)
  set(spdlog_VERSION "1.17.0" CACHE STRING "spdlog version tag")
  if(CMAKE_CXX_STANDARD LESS 20)
    set(fmt_VERSION "12.2.0" CACHE STRING "fmt version tag")
  endif()
  include("${NESTDAQ_DEPENDENCIES_CMAKE_DIR}/spdlog.cmake")
endif()

if(WITH_REDIS_STACK)
  set(Redis_VERSION "8.2.9" CACHE STRING "Redis version tag")
  set(RedisBloom_VERSION "8.2.16" CACHE STRING "RedisBloom version tag")
  set(RediSearch_VERSION "8.2.13" CACHE STRING "RediSearch version tag")
  set(RedisJSON_VERSION "8.2.9" CACHE STRING "RedisJSON version tag")
  set(RedisTimeSeries_VERSION "8.2.10" CACHE STRING "RedisTimeSeries version tag")
  include("${NESTDAQ_DEPENDENCIES_CMAKE_DIR}/redis-stack.cmake")
endif()
if(WITH_REDIS_SERVER_7)
  include("${NESTDAQ_DEPENDENCIES_CMAKE_DIR}/redis-server-7.cmake")
endif()

set(hiredis_VERSION "1.4.1" CACHE STRING "hiredis version tag")
include("${NESTDAQ_DEPENDENCIES_CMAKE_DIR}/hiredis.cmake")

set(redis_plus_plus_VERSION "1.3.15" CACHE STRING "redis++ version tag")
include("${NESTDAQ_DEPENDENCIES_CMAKE_DIR}/redis_plus_plus.cmake")

option(WITH_OTEL_CPP "Build and install opentelemetry-cpp" ON)
if(WITH_OTEL_CPP)
  set(opentelemetry-cpp_VERSION "1.28.0" CACHE STRING "opentelemetry-cpp version tag")
  include("${NESTDAQ_DEPENDENCIES_CMAKE_DIR}/opentelemetry-cpp.cmake")
endif()

find_package(Doxygen QUIET)
if(Doxygen_FOUND)
  set(doxygen-awesome-css_VERSION "2.4.2" CACHE STRING "doxygen-awesome-css version tag")
  include("${NESTDAQ_DEPENDENCIES_CMAKE_DIR}/doxygen-awesome-css.cmake")
else()
  message(STATUS "Doxygen not found: skipping doxygen-awesome-css")
endif()
