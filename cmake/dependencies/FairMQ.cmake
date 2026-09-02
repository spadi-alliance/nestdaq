# 1. Find the insalled FairMQ using find_package()
# 2. If the package is not found,  fetch and build FairMQ from GitHub

message(STATUS "========== include FairMQ.cmake ==========")
set(FairMQ_GIT_TAG "v${FairMQ_VERSION}")

# FairMQ uses CMAKE_SOURCE_DIR internally. 
# FetchContent causes this variable to point to the parent project's root rather than FairMQ's own source directory.

find_package(FairMQ ${FairMQ_VERSION} QUIET)

if(FairMQ_FOUND)
  message(STATUS "Found FairMQ")

else()
  message(STATUS "FairMQ not found. --- fetch from GitHub")

  include(ExternalProject)
  ExternalProject_Add(
    FairMQ
    GIT_REPOSITORY https://github.com/FairRootGroup/FairMQ.git
    GIT_TAG        ${FairMQ_GIT_TAG}

    DEPENDS ZeroMQ boost FairLogger

    # linker flags must be empty
    CMAKE_ARGS
      -DCMAKE_BUILD_TYPE=Release
      -DCMAKE_INSTALL_PREFIX=${CMAKE_INSTALL_PREFIX}
      -DCMAKE_PREFIX_PATH=${CMAKE_INSTALL_PREFIX}
      -DCMAKE_CXX_STANDARD=${CMAKE_CXX_STANDARD}
      -DCMAKE_LINKER_TYPE=${CMAKE_LINKER_TYPE}
      -DCMAKE_LINKER=${CMAKE_LINKER}
      -DCMAKE_EXE_LINKER_FLAGS=
      -DCMAKE_SHARED_LINKER_FLAGS=
    
    BUILD_COMMAND
      ${CMAKE_COMMAND} --build <BINARY_DIR> --config $<CONFIG> ${BUILD_PARALLEL_LEVEL} 
  )
endif()

message(STATUS "========== include FairMQ.cmake done ==========")