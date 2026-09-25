message(STATUS "========== include Catch2.cmake ==========")

find_package(Catch2 ${Catch2_VERSION} CONFIG QUIET)

if(Catch2_FOUND)
  message(STATUS "Found Catch2")
else()
  include(ExternalProject)

  set(Catch2_RELEASE_TAG "v${Catch2_VERSION}")
  set(Catch2_RELEASE_URL
    "https://github.com/catchorg/Catch2/archive/refs/tags/${Catch2_RELEASE_TAG}.tar.gz")

  message(STATUS "Catch2 not found. --- downloading from ${Catch2_RELEASE_URL}")

  ExternalProject_Add(
    Catch2
    URL ${Catch2_RELEASE_URL}
    ${NESTDAQ_DOWNLOAD_EXTRACT_TIMESTAMP_ARGS}
    UPDATE_COMMAND ""
    CMAKE_ARGS
      -DCMAKE_INSTALL_PREFIX=<INSTALL_DIR>
      -DCMAKE_BUILD_TYPE=Release
      -DCMAKE_CXX_STANDARD=${CMAKE_CXX_STANDARD}
      -DBUILD_TESTING=OFF
      -DCMAKE_SHARED_LINKER_FLAGS=${CMAKE_SHARED_LINKER_FLAGS}
    BUILD_COMMAND
      ${CMAKE_COMMAND} --build <BINARY_DIR> --config $<CONFIG> ${BUILD_PARALLEL_LEVEL}
    INSTALL_COMMAND
      ${CMAKE_COMMAND} --install <BINARY_DIR> --config $<CONFIG>
    INSTALL_DIR ${CMAKE_INSTALL_PREFIX}
  )
endif()

message(STATUS "========== include Catch2.cmake done ==========")
