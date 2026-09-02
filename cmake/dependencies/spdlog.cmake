message(STATUS "========== include spdlog.cmake ==========")

find_package(spdlog ${spdlog_VERSION} CONFIG QUIET)

if(spdlog_FOUND)
  message(STATUS "Found spdlog")
else()
  include(ExternalProject)

  set(spdlog_DEPENDS)
  set(spdlog_FORMAT_ARGS)
  if(CMAKE_CXX_STANDARD LESS 20)
    set(fmt_VERSION "12.2.0" CACHE STRING "fmt version tag")
    include("${NESTDAQ_DEPENDENCIES_CMAKE_DIR}/fmt.cmake")
    list(APPEND spdlog_DEPENDS
      ${NESTDAQ_FMT_DEPENDS}
    )
    list(APPEND spdlog_FORMAT_ARGS
      -DSPDLOG_USE_STD_FORMAT=OFF
      -DSPDLOG_FMT_EXTERNAL=ON
      ${NESTDAQ_FMT_CMAKE_ARGS}
    )
  else()
    list(APPEND spdlog_FORMAT_ARGS
      -DSPDLOG_USE_STD_FORMAT=ON
      -DSPDLOG_FMT_EXTERNAL=OFF
    )
  endif()

  set(spdlog_RELEASE_TAG "v${spdlog_VERSION}")
  set(spdlog_RELEASE_URL
    "https://github.com/gabime/spdlog/archive/refs/tags/${spdlog_RELEASE_TAG}.tar.gz")

  message(STATUS "spdlog not found. --- downloading from ${spdlog_RELEASE_URL}")

  ExternalProject_Add(
    spdlog
    URL ${spdlog_RELEASE_URL}
    ${NESTDAQ_DOWNLOAD_EXTRACT_TIMESTAMP_ARGS}
    UPDATE_COMMAND ""
    DEPENDS
      ${spdlog_DEPENDS}
    CMAKE_ARGS
      -DCMAKE_INSTALL_PREFIX=<INSTALL_DIR>
      -DCMAKE_BUILD_TYPE=Release
      -DCMAKE_CXX_STANDARD=${CMAKE_CXX_STANDARD}
      -DSPDLOG_BUILD_SHARED=ON
      ${spdlog_FORMAT_ARGS}
      -DSPDLOG_BUILD_TESTS=OFF
      -DSPDLOG_BUILD_TESTS_HO=OFF
      -DSPDLOG_BUILD_EXAMPLE=OFF
      -DSPDLOG_BUILD_EXAMPLE_HO=OFF
      -DSPDLOG_BUILD_BENCH=OFF
      -DCMAKE_LINKER_TYPE=${CMAKE_LINKER_TYPE}
      -DCMAKE_LINKER=${CMAKE_LINKER}
      -DCMAKE_EXE_LINKER_FLAGS=${CMAKE_EXE_LINKER_FLAGS}
      -DCMAKE_SHARED_LINKER_FLAGS=${CMAKE_SHARED_LINKER_FLAGS}
    BUILD_COMMAND
      ${CMAKE_COMMAND} --build <BINARY_DIR> --config $<CONFIG> ${BUILD_PARALLEL_LEVEL}
    INSTALL_COMMAND
      ${CMAKE_COMMAND} --install <BINARY_DIR> --config $<CONFIG>
    INSTALL_DIR ${CMAKE_INSTALL_PREFIX}
  )
endif()

message(STATUS "========== include spdlog.cmake done ==========")
