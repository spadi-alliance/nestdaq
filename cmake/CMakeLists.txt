cmake_minimum_required(VERSION 3.24)

project(nestdaq_external_dependencies LANGUAGES CXX C)

include(common.cmake)

#------------------------------------------------------------------------------
if(${PROJECT_NAME} STREQUAL "nestdaq")
  set(MY_CMAKE_FILES "cmake/")
endif()

set(BOOST_BUILD_PARALLEL_LEVEL "")
if(BUILD_PARALLEL_LEVEL)
  set(BOOST_BUILD_PARALLEL_LEVEL "-j${BUILD_PARALLEL_LEVEL}")
  # replace the value
  set(BUILD_PARALLEL_LEVEL "--parallel ${BUILD_PARALLEL_LEVEL}")
endif()

set(ZeroMQ_VERSION "4.3.5" CACHE STRING "ZeroMQ version tag")
include(${MY_CMAKE_FILES}ZeroMQ.cmake)

set(Boost_VERSION "1.85.0" CACHE STRING "boost version tag")
include(${MY_CMAKE_FILES}Boost.cmake)

set(FairLogger_VERSION "2.3.0" CACHE STRING "FairLogger version tag")
include(${MY_CMAKE_FILES}FairLogger.cmake)

set(FairMQ_VERSION "1.10.0" CACHE STRING "FairMQ version tag")
include(${MY_CMAKE_FILES}FairMQ.cmake)

set(Redis_VERSION "8.6.2" CACHE STRING "Redis version tag")
set(RedisBloom_VERSION "2.8.17" CACHE STRING "RedisBloom version tag")
set(RediSearch_VERSION "2.10.25" CACHE STRING "RediSearch version tag")
set(RedisJSON_VERSION "2.8.16" CACHE STRING "RedisJSON version tag")
set(RedisTimeSeries_VERSION "1.12.9" CACHE STRING "RedisTimeSeries version tag")
include(${MY_CMAKE_FILES}redis-stack.cmake)

set(hiredis_VERSION "1.3.0" CACHE STRING "hiredis version tag")
include(${MY_CMAKE_FILES}hiredis.cmake)

set(redis_plus_plus_VERSION "1.3.15" CACHE STRING "redis++ version tag")
include(${MY_CMAKE_FILES}redis_plus_plus.cmake)

if(WITH_OTEL_CPP)
  set(opentelemetry-cpp_VERSION "1.24.0" CACHE STRING "opentelemetry-cpp version tag")
  include(${MY_CMAKE_FILES}opentelemetry-cpp.cmake)
endif()
