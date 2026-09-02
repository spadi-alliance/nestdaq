# 1. Find the insalled hiredis package using find_package()
# 2. If the package is not found, fetch and build hiredis from GitHub by ExternalProject_Add()

message(STATUS "========== include hiredis.cmake ==========")

set(hiredis_GIT_TAG "v${hiredis_VERSION}")

find_package(hiredis ${hiredis_VERSION} QUIET)
if(hiredis_FOUND)
  message(STATUS "Found hiredis")
else()
  message(STATUS "hiredis not found. --- fetch from GitHub")

  include(ExternalProject)
  ExternalProject_Add(
    hiredis
    GIT_REPOSITORY https://github.com/redis/hiredis.git
    GIT_TAG        ${hiredis_GIT_TAG}

    CMAKE_ARGS
      -DCMAKE_BUILD_TYPE=Release
      -DCMAKE_INSTALL_PREFIX=${CMAKE_INSTALL_PREFIX}
      -DCMAKE_LINKER_TYPE=${CMAKE_LINKER_TYPE}
      -DCMAKE_LINKER=${CMAKE_LINKER}
      -DCMAKE_EXE_LINKER_FLAGS=${CMAKE_EXE_LINKER_FLAGS}
      -DCMAKE_SHARED_LINKER_FLAGS=${CMAKE_SHARED_LINKER_FLAGS}
    
    BUILD_COMMAND
      ${CMAKE_COMMAND} --build <BINARY_DIR> --config $<CONFIG> ${BUILD_PARALLEL_LEVEL} 
    
  )
endif()


message(STATUS "========== include hiredis.cmake done ==========")
