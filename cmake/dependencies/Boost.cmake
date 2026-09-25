# 1. Find the insalled Boost package using find_package()
# 2. If the package is not found, download and build a Boost release archive by ExternalProject_Add

message(STATUS "========== include Boost.cmake ==========")
set(Boost_RELEASE_TAG "boost-${Boost_VERSION}")
set(Boost_RELEASE_ARCHIVE "boost-${Boost_VERSION}-cmake.tar.xz")
set(Boost_RELEASE_URL "https://github.com/boostorg/boost/releases/download/${Boost_RELEASE_TAG}/${Boost_RELEASE_ARCHIVE}")


find_package(Boost) # ${Boost_VERSION} QUIET)

if(Boost_FOUND)
  message(STATUS "Found Boost")

else()
  message(STATUS "Boost not found. --- downloading from ${Boost_RELEASE_URL}")

  include(ExternalProject)
  ExternalProject_Add(
      boost
      URL                    ${Boost_RELEASE_URL}
      ${NESTDAQ_DOWNLOAD_EXTRACT_TIMESTAMP_ARGS}
      UPDATE_COMMAND "" # skip update command
      CMAKE_ARGS
        -DCMAKE_INSTALL_PREFIX=<INSTALL_DIR>
        -DCMAKE_BUILD_TYPE=Release
        -DCMAKE_CXX_STANDARD=${CMAKE_CXX_STANDARD}
        -DBUILD_SHARED_LIBS=ON
        -DCMAKE_SHARED_LINKER_FLAGS=${CMAKE_SHARED_LINKER_FLAGS}
      BUILD_COMMAND
        ${CMAKE_COMMAND} --build <BINARY_DIR> --config $<CONFIG> ${BUILD_PARALLEL_LEVEL}
      INSTALL_COMMAND
        ${CMAKE_COMMAND} --install <BINARY_DIR> --config $<CONFIG>

      INSTALL_DIR ${CMAKE_INSTALL_PREFIX}
  )
endif()


message(STATUS "========== include Boost.cmake done ==========")
