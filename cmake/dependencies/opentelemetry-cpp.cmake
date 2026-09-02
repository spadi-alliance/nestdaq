# 1. Find the insalled opentelemetry-cpp using find_package()
# 2. If the package is not found, fetch and build opentelemetry-cpp from GitHub


message(STATUS "========== include opentelemetry-cpp.cmake ==========")

set(opentelemetry-cpp_GIT_TAG "v${opentelemetry-cpp_VERSION}")

find_package(opentelemetry-cpp ${opentelemetry-cpp_VERSION} QUIET)
if(opentelemetry-cpp_FOUND)
  message(STATUS "Found opentelemetry-cpp")
else()
  message(STATUS "opentelemetry-cpp not found. --- fetch from GitHub")

  include(ExternalProject)
  ExternalProject_Add(
    opentelemetry-cpp
    GIT_REPOSITORY https://github.com/open-telemetry/opentelemetry-cpp
    GIT_TAG        ${opentelemetry-cpp_GIT_TAG}

    CMAKE_ARGS
      -DCMAKE_BUILD_TYPE=Release
      -DCMAKE_INSTALL_PREFIX=${CMAKE_INSTALL_PREFIX}
      -DCMAKE_PREFIX_PATH=${CMAKE_INSTALL_PREFIX}
      -DCMAKE_CXX_STANDARD=${CMAKE_CXX_STANDARD}
      -DBUILD_SHARED_LIBS=ON
      -DCMAKE_POSITION_INDEPENDENT_CODE=ON
      -DCMAKE_INSTALL_RPATH=$ORIGIN
      -DCMAKE_LINKER_TYPE=${CMAKE_LINKER_TYPE}
      -DCMAKE_LINKER=${CMAKE_LINKER}
      -DCMAKE_EXE_LINKER_FLAGS=${CMAKE_EXE_LINKER_FLAGS}
      -DCMAKE_SHARED_LINKER_FLAGS=${CMAKE_SHARED_LINKER_FLAGS}
      -DBUILD_TESTING=OFF
      -DWITH_BENCHMARK=OFF
      -DWITH_OTLP_GRPC=ON
      -DWITH_OTLP_HTTP=ON
      -DWITH_PROMETHEUS=OFF
    
    BUILD_COMMAND
      ${CMAKE_COMMAND} --build <BINARY_DIR> --config $<CONFIG> ${BUILD_PARALLEL_LEVEL} 
  )

  if(TARGET nlohmann_json)
    add_dependencies(opentelemetry-cpp nlohmann_json)
  endif()
endif()


message(STATUS "========== include opentelemetry-cpp.cmake done ==========")
