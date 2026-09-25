#------------------------------------------------------------------------------
# C++ standard level
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(MY_CXX_STANDARD 17)
if(NOT CMAKE_CXX_STANDARD)
  set(CMAKE_CXX_STANDARD ${MY_CXX_STANDARD})
elseif(${CMAKE_CXX_STANDARD} LESS ${MY_CXX_STANDARD})
  message(FATAL_ERROR "A minimum CMAKE_CXX_STANDARD of ${MY_CXX_STANDARD} is required.")
endif()
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_POSITION_INDEPENDENT_CODE ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# ExternalProject_Add supports DOWNLOAD_EXTRACT_TIMESTAMP only with CMake 3.24
# or later. Keep it conditional so dependency builds still work with CMake 3.22.
set(NESTDAQ_DOWNLOAD_EXTRACT_TIMESTAMP_ARGS)
if(CMAKE_VERSION VERSION_GREATER_EQUAL 3.24)
  set(NESTDAQ_DOWNLOAD_EXTRACT_TIMESTAMP_ARGS DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
endif()

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra -Wshadow -Wfloat-equal")
message(STATUS "")
message(STATUS "CMAKE_PREFIX_PATH:              ${CMAKE_PREFIX_PATH}")
message(STATUS "CMAKE_CXX_FLAGS:                ${CMAKE_CXX_FLAGS}")
message(STATUS "CMAKE_CXX_FLAGS_DEBUG:          ${CMAKE_CXX_FLAGS_DEBUG}")
message(STATUS "CMAKE_LINKER_TYPE:              ${CMAKE_LINKER_TYPE}")
message(STATUS "CMAKE_LINKER:                   ${CMAKE_LINKER}")
message(STATUS "CMAKE_EXE_LINKER_FLAGS:         ${CMAKE_EXE_LINKER_FLAGS}")
message(STATUS "CMAKE_SHARED_LINKER_FLAGS:      ${CMAKE_SHARED_LINKER_FLAGS}")
message(STATUS "CMAKE_CXX_FLAGS_RELWITHDEBINFO: ${CMAKE_CXX_FLAGS_RELWITHDEBINFO}")
message(STATUS "CMAKE_CXX_FLAGS_RELEASE:        ${CMAKE_CXX_FLAGS_RELEASE}")
message(STATUS "CMAKE_CXX_FLAGS_MINSIZEREL:     ${CMAKE_CXX_FLAGS_MINSIZEREL}")
message(STATUS "CMAKE_BUILD_TYPE:               ${CMAKE_BUILD_TYPE}")
message(STATUS "")

#------------------------------------------------------------------------------
include(GNUInstallDirs)
message(STATUS "CMAKE_INSTALL_PREFIX:     ${CMAKE_INSTALL_PREFIX}")
message(STATUS "CMAKE_INSTALL_INCLUDEDIR: ${CMAKE_INSTALL_INCLUDEDIR}")
message(STATUS "CMAKE_INSTALL_LIBDIR:     ${CMAKE_INSTALL_LIBDIR}")
message(STATUS "CMAKE_INSTALL_BINDIR:     ${CMAKE_INSTALL_BINDIR}")
message(STATUS "CMAKE_INSTALL_DATADIR:    ${CMAKE_INSTALL_DATADIR}")
message(STATUS "")

#------------------------------------------------------------------------------
find_package(Threads REQUIRED)
