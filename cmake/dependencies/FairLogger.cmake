# 1. Find the insalled Boost package using find_package()
# 2. If the package is not found, fetch and build Boost from GitHub

message(STATUS "========== include FairLogger.cmake ==========")
set(FairLogger_GIT_TAG "v${FairLogger_VERSION}")

# FairLogger uses CMAKE_SOURCE_DIR internally. 
# FetchContent causes this variable to point to the parent project's root rather than FairLogger's own source directory.

find_package(FairLogger ${FairLogger_VERSION} QUIET)
if(FairLogger_FOUND)
  message(STATUS "Found FairLogger")

else()
  message(STATUS "FairLogger not found. --- fetching from GitHub")

  include(ExternalProject)
  ExternalProject_Add(
    FairLogger
    GIT_REPOSITORY https://github.com/FairRootGroup/FairLogger.git
    GIT_TAG        ${FairLogger_GIT_TAG}

    DEPENDS boost

    CMAKE_ARGS
      -DEXTERNAL_FMT=OFF 
      -DCMAKE_BUILD_TYPE=Release
      -DCMAKE_INSTALL_PREFIX=${CMAKE_INSTALL_PREFIX}
      -DCMAKE_PREFIX_PATH=${CMAKE_INSTALL_PREFIX}
      -DCMAKE_CXX_STANDARD=${CMAKE_CXX_STANDARD}
      -DCMAKE_LINKER_TYPE=${CMAKE_LINKER_TYPE}
      -DCMAKE_LINKER=${CMAKE_LINKER}
      -DCMAKE_EXE_LINKER_FLAGS=${CMAKE_EXE_LINKER_FLAGS}
      -DCMAKE_SHARED_LINKER_FLAGS=${CMAKE_SHARED_LINKER_FLAGS}
    
    BUILD_COMMAND
      ${CMAKE_COMMAND} --build <BINARY_DIR> --config $<CONFIG> ${BUILD_PARALLEL_LEVEL} 
  )
endif()

message(STATUS "========== include FairLogger.cmake done ==========")