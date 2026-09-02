# 1. Find the insalled redis_plus_plus package using find_package()
# 2. If the package is not found, fetch and build redis_plus_plus from GitHub by ExternalProject_Add()

message(STATUS "========== include redis_plus_plus.cmake ==========")

set(redis_plus_plus_GIT_TAG "${redis_plus_plus_VERSION}")

find_package(redis++ ${redis_plus_plus_VERSION} QUIET)
if(redis++_FOUND)
  message(STATUS "Found redis++")
else()
  message(STATUS "redis++ not found. --- fetch from GitHub")

  include(ExternalProject)
  ExternalProject_Add(
    redis_plus_plus
    GIT_REPOSITORY https://github.com/sewenew/redis-plus-plus.git
    GIT_TAG        ${redis_plus_plus_GIT_TAG}

    DEPENDS hiredis

    CMAKE_ARGS
      -DCMAKE_BUILD_TYPE=Release
      -DCMAKE_INSTALL_PREFIX=${CMAKE_INSTALL_PREFIX}
      -DCMAKE_PREFIX_PATH=${CMAKE_INSTALL_PREFIX}
      -DCMAKE_LINKER_TYPE=${CMAKE_LINKER_TYPE}
      -DCMAKE_LINKER=${CMAKE_LINKER}
      -DCMAKE_EXE_LINKER_FLAGS=${CMAKE_EXE_LINKER_FLAGS}
      -DCMAKE_SHARED_LINKER_FLAGS=${CMAKE_SHARED_LINKER_FLAGS}
      -DREDIS_PLUS_PLUS_CXX_STANDARD=${CMAKE_CXX_STANDARD}
      -DREDIS_PLUS_PLUS_BUILD_TEST=OFF
    
    BUILD_COMMAND
      ${CMAKE_COMMAND} --build <BINARY_DIR> --config $<CONFIG> ${BUILD_PARALLEL_LEVEL} 
  )
endif()


message(STATUS "========== include redis_plus_plus.cmake done ==========")