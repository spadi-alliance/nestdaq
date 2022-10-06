cmake_minimum_required(VERSION 3.11)

project(E16DAQController LANGUAGES CXX C)

message(STATUS "PROJECT_NAME: ${PROJECT_NAME}")

# check c++ compiler and turn on colored output
message("c++ compiler ID is ${CMAKE_CXX_COMPILER_ID}")
if("${CMAKE_CXX_COMPILER_ID}" MATCHES "GNU")
  message("turnn on colored error message of GNU")
  add_compile_options(-fdiagnostics-color=always)
elseif("${CMAKE_CXX_COMPILER_ID}" MATCHES "Clang")
  message("turn on colored error message of Clang")
  add_compiler_options(-fcolor-diagnostics)
endif()

#------------------------------------------------------------------------------
# C++ standard level
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(MY_CXX_STANDARD 17)
if(NOT CMAKE_CXX_STANDARD)
  set(CMAKE_CXX_STANDARD ${MY_CXX_STANDARD})
elseif(${CMAKE_CXX_STANDARD} LESS ${MY_CXX_STANDARD})
  message(FATAL_ERROR "A minimum CMAKE_CXX_STANDARD of ${MY_CXX_STANDARD} is required.")
elseif(${CMAKE_CXX_STANDARD} GREATER ${MY_CXX_STANDARD})
  message(WARNING "A CMAKE_CXX_STANDARD of ${CMAKE_CXX_STANDARD} (greater than ${MY_CXX_STANDARD}) is not tested. Use on your on risk.")
endif()
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_POSITION_INDEPENDENT_CODE ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra -Wshadow -Wfloat-equal")
set(CMAKE_CXX_FLAGS_RELEASE "-Ofast -DNDEBUG -march=native")
message("")
message("CMAKE_CXX_FLAGS:                ${CMAKE_CXX_FLAGS}")
message("CMAKE_CXX_FLAGS_DEBUG:          ${CMAKE_CXX_FLAGS_DEBUG}")
message("CMAKE_CXX_FLAGS_RELWITHDEBINFO: ${CMAKE_CXX_FLAGS_RELWITHDEBINFO}")
message("CMAKE_CXX_FLAGS_RELEASE:        ${CMAKE_CXX_FLAGS_RELEASE}")
message("CMAKE_CXX_FLAGS_MINSIZEREL:     ${CMAKE_CXX_FLAGS_MINSIZEREL}")
message("CMAKE_BUILD_TYPE:               ${CMAKE_BUILD_TYPE}")
message("")

#------------------------------------------------------------------------------
include(GNUInstallDirs)
message("CMAKE_INSTALL_PREFIX:     ${CMAKE_INSTALL_PREFIX}")
message("CMAKE_INSTALL_INCLUDEDIR: ${CMAKE_INSTALL_INCLUDEDIR}")
message("CMAKE_INSTALL_LIBDIR:     ${CMAKE_INSTALL_LIBDIR}")
message("CMAKE_INSTALL_BINDIR:     ${CMAKE_INSTALL_BINDIR}")
message("CMAKE_INSTALL_DATADIR:    ${CMAKE_INSTALL_DATADIR}")
message("")

find_package(Threads REQUIRED)

#------------------------------------------------------------------------------
# RPATH handling
set(CMAKE_INSTALL_RPATH_USE_LINK_PATH TRUE)
list(FIND CMAKE_PLATFORM_IMPLICIT_LINK_DIRECTORIES "${CMAKE_INSTALL_PREFIX/${CMAKE_INSTALL_LIBDIR}}" isSystemDir)
if("${isSystemDir}" STREQUAL "-1")
  if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    set(CMAKE_EXE_LINKER_FLAGS    "${CMAKE_EXE_LINKER_FLAGS} -Wl,--enable-new-dtags")
    set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} -Wl,--enable-new-dtags")
    set(CMAKE_INSTALL_RPATH       "$ORIGIN/../${CMAKE_INSTALL_LIBDIR}")
  elseif(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
    set(CMAKE_INSTALL_RPATH "@loader_path/../${CMAKE_INSTALL_LIBDIR}")
  else()
    set(CMAKE_INSTALL_RPATH "${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_LIBDIR}")
  endif()
endif()

message("CMAKE_EXE_LINKER_FLAGS:    ${CMAKE_EXE_LINKER_FLAGS}")
message("CMAKE_SHARED_LINKER_FLAGS: ${CMAKE_SHARED_LINKER_FLAGS}")

#------------------------------------------------------------------------------
# set up FairMQ and Boost
find_package(FairMQ)

if(FairMQ_FOUND)
  message(STATUS "FairMQ found")
  message(STATUS "FairMQ_VERSION:              ${FairMQ_VERSION}")
  message(STATUS "FairMQ_GIT_VERSION:          ${FairMQ_GIT_VERSION}")
  message(STATUS "FairMQ_PACKAGE_DEPENDENCIES: ${FairMQ_PACKAGE_DEPENDENCIES}")
  message(STATUS "FairMQ_PACKAGE_COMPONENTS:   ${FairMQ_PACKAGE_COMPONENTS}")
  message(STATUS "FairMQ_PREFIX:               ${FairMQ_PREFIX}")
  message(STATUS "FairMQ_BINDIR:               ${FairMQ_BINDIR}")
  message(STATUS "FairMQ_INCDIR:               ${FairMQ_INCDIR}")
  message(STATUS "FairMQ_LIBDIR:               ${FairMQ_LIBDIR}")
  message(STATUS "FairMQ_DATADIR:              ${FairMQ_DATADIR}")
  message(STATUS "FairMQ_CMAKEMODDIR:          ${FairMQ_CMAKEMODDIR}")
  message(STATUS "FairMQ_BUILD_TYPE:           ${FairMQ_BUILD_TYPE}")
  message(STATUS "FairMQ_CXX_FLAGS:            ${FairMQ_CXX_FLAGS}")

  foreach(dep IN LISTS FairMQ_PACKAGE_DEPENDENCIES)
    if(FairMQ_${dep}_COMPONENTS)
      message(" dep: ${dep}")
      message("  FairMQ_${dep}_version:    ${FairMQ_${dep}_VERSION}")
      message("  FairMQ_${dep}_COMPONENTS: ${FairMQ_${dep}_COMPONENTS}")
      if(${dep} MATCHES "Boost")
        find_package(${dep} ${FairMQ_${dep}_VERSION} COMPONENTS ${FairMQ_${dep}_COMPONENTS}; iostreams; thread;)
      else()
        find_package(${dep} ${FairMQ_${dep}_VERSION} COMPONENTS ${FairMQ_${dep}_COMPONENTS};)
      endif()
    else()
      message(" dep: ${dep}")
      message("  FairMQ_${dep}_VERSION: ${FairMQ_${dep}_VERSION}")
      find_package(${dep} ${FairMQ_${dep}_VERSION})
    endif()
  endforeach()
else()
  message(WARNING "FairMQ not found")
endif()


message(STATUS "Boost_INCLUDE_DIRS: ${Boost_INCLUDE_DIRS}")
message(STATUS "Boost_LIBRARY_DIRS: ${Boost_LIBRARY_DIRS}")
message(STATUS "Boost_LIBRARIES:    ${Boost_LIBRARIES}")

if(NOT FairLogger_INCDIR)
  set(FairLogger_INCDIR "${FairMQ_INCDIR}/fairlogger/bundled")
endif()
message(STATUS "FairLogger_INCDIR: ${FairLogger_INCDIR}")
message(STATUS "FairLogger_LIBDIR: ${FairLogger_LIBDIR}")

if(fmt_DIR)
  message(STATUS "fmt_DIR: ${fmt_DIR}")
  set(fmt_LIB fmt::fmt)
  message(STATUS "fmt_LIB: ${fmt_LIB}")
endif()


#------------------------------------------------------------------------------
add_subdirectory(plugins)
add_subdirectory(examples)
add_subdirectory(controller)
add_subdirectory(share)
add_subdirectory(scripts)
