message(STATUS "========== include fmt.cmake ==========")

find_package(fmt ${fmt_VERSION} CONFIG QUIET)

set(NESTDAQ_FMT_DEPENDS)
set(NESTDAQ_FMT_CMAKE_ARGS)

if(fmt_FOUND)
  message(STATUS "Found fmt")
  if(fmt_DIR)
    list(APPEND NESTDAQ_FMT_CMAKE_ARGS
      -Dfmt_DIR=${fmt_DIR}
    )
  endif()
else()
  include(ExternalProject)

  set(fmt_RELEASE_TAG "${fmt_VERSION}")
  set(fmt_RELEASE_URL
    "https://github.com/fmtlib/fmt/archive/refs/tags/${fmt_RELEASE_TAG}.tar.gz")

  message(STATUS "fmt not found. --- downloading from ${fmt_RELEASE_URL}")

  ExternalProject_Add(
    fmt
    URL ${fmt_RELEASE_URL}
    ${NESTDAQ_DOWNLOAD_EXTRACT_TIMESTAMP_ARGS}
    UPDATE_COMMAND ""
    CMAKE_ARGS
      -DCMAKE_INSTALL_PREFIX=<INSTALL_DIR>
      -DCMAKE_BUILD_TYPE=Release
      -DCMAKE_CXX_STANDARD=${CMAKE_CXX_STANDARD}
      -DCMAKE_POSITION_INDEPENDENT_CODE=ON
      -DFMT_DOC=OFF
      -DFMT_TEST=OFF
      -DFMT_INSTALL=ON
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

  set(NESTDAQ_FMT_DEPENDS fmt)
  list(APPEND NESTDAQ_FMT_CMAKE_ARGS
    -DCMAKE_PREFIX_PATH=<INSTALL_DIR>
  )
endif()

message(STATUS "========== include fmt.cmake done ==========")
