message(STATUS "========== include nlohmann_json.cmake ==========")

find_package(nlohmann_json ${nlohmann_json_VERSION} CONFIG QUIET)

if(nlohmann_json_FOUND)
  message(STATUS "Found nlohmann_json")
else()
  include(ExternalProject)

  set(nlohmann_json_RELEASE_TAG "v${nlohmann_json_VERSION}")
  set(nlohmann_json_RELEASE_URL
    "https://github.com/nlohmann/json/archive/refs/tags/${nlohmann_json_RELEASE_TAG}.tar.gz")

  message(STATUS "nlohmann_json not found. --- downloading from ${nlohmann_json_RELEASE_URL}")

  ExternalProject_Add(
    nlohmann_json
    URL ${nlohmann_json_RELEASE_URL}
    ${NESTDAQ_DOWNLOAD_EXTRACT_TIMESTAMP_ARGS}
    UPDATE_COMMAND ""
    CMAKE_ARGS
      -DCMAKE_INSTALL_PREFIX=<INSTALL_DIR>
      -DCMAKE_BUILD_TYPE=Release
      -DCMAKE_CXX_STANDARD=${CMAKE_CXX_STANDARD}
      -DJSON_BuildTests=OFF
      -DJSON_Install=ON
      -DCMAKE_SHARED_LINKER_FLAGS=${CMAKE_SHARED_LINKER_FLAGS}
    BUILD_COMMAND
      ${CMAKE_COMMAND} --build <BINARY_DIR> --config $<CONFIG> ${BUILD_PARALLEL_LEVEL}
    INSTALL_COMMAND
      ${CMAKE_COMMAND} --install <BINARY_DIR> --config $<CONFIG>
    INSTALL_DIR ${CMAKE_INSTALL_PREFIX}
  )
endif()

message(STATUS "========== include nlohmann_json.cmake done ==========")
