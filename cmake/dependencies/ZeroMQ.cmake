# 1. Find the insalled ZeroMQ using find_package()
# 2. If the package is not found, fetch and build ZeroMQ from GitHub

message(STATUS "========== include ZeroMQ.cmake ==========")

set(ZeroMQ_GIT_TAG "v${ZeroMQ_VERSION}")

find_package(ZeroMQ ${ZeroMQ_VERSION} QUIET)
if(ZeroMQ_FOUND)
  message(STATUS "Found ZeroMQ")
else()
  message(STATUS "ZeroMQ not found. --- fetch from GitHub")

  include(ExternalProject)
  ExternalProject_Add(
    ZeroMQ
    GIT_REPOSITORY https://github.com/zeromq/libzmq
    GIT_TAG        ${ZeroMQ_GIT_TAG}

    CMAKE_ARGS
      -Wno-dev
      -DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE}
      -DCMAKE_INSTALL_PREFIX=${CMAKE_INSTALL_PREFIX}
      -DCMAKE_CXX_STANDARD=${CMAKE_CXX_STANDARD}
      -DCMAKE_LINKER_TYPE=${CMAKE_LINKER_TYPE}
      -DCMAKE_LINKER=${CMAKE_LINKER}
      -DCMAKE_EXE_LINKER_FLAGS=${CMAKE_EXE_LINKER_FLAGS}
      -DCMAKE_SHARED_LINKER_FLAGS=${CMAKE_SHARED_LINKER_FLAGS}
      -DZMQ_BUILD_TESTS=OFF

    BUILD_COMMAND
      ${CMAKE_COMMAND} --build <BINARY_DIR> --config $<CONFIG> ${BUILD_PARALLEL_LEVEL} 
  )
endif()


message(STATUS "========== include ZeroMQ.cmake done ==========")