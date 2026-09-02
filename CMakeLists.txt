cmake_minimum_required(VERSION 3.22)

include(cmake/GitHelper.cmake)

project(NestDAQ VERSION "${PROJECT_VERSION}" LANGUAGES CXX C)

message(STATUS "PROJECT_NAME: ${PROJECT_NAME}")

include(cmake/common.cmake)
include(cmake/NestDAQBuildSettings.cmake)
include(CheckCXXSourceCompiles)
include(CMakePackageConfigHelpers)
include(CTest)

nestdaq_enable_clang_tidy("Run clang-tidy during NestDAQ builds")

option(NestDAQ_BUILD_DOCS "Build and install NestDAQ Doxygen documentation" OFF)
option(NestDAQ_BUILD_EXAMPLES "Build and install NestDAQ example devices" ON)

set(NESTDAQ_CMAKE_INSTALL_DIR "${CMAKE_INSTALL_LIBDIR}/cmake/NestDAQ")

write_basic_package_version_file(
  "${CMAKE_CURRENT_BINARY_DIR}/cmake/NestDAQConfigVersion.cmake"
  VERSION "${PROJECT_VERSION}"
  COMPATIBILITY SameMajorVersion
)

configure_file(
  "${CMAKE_CURRENT_SOURCE_DIR}/cmake/NestDAQVersion.cmake.in"
  "${CMAKE_CURRENT_BINARY_DIR}/cmake/NestDAQVersion.cmake"
  @ONLY
)

install(FILES
  "${CMAKE_CURRENT_BINARY_DIR}/cmake/NestDAQConfig.cmake"
  "${CMAKE_CURRENT_BINARY_DIR}/cmake/NestDAQConfigVersion.cmake"
  "${CMAKE_CURRENT_BINARY_DIR}/cmake/NestDAQVersion.cmake"
  "${CMAKE_CURRENT_SOURCE_DIR}/cmake/NestDAQTargets.cmake"
  DESTINATION "${NESTDAQ_CMAKE_INSTALL_DIR}"
)

#------------------------------------------------------------------------------
# RPATH handling
nestdaq_configure_install_rpath()

#------------------------------------------------------------------------------
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

configure_package_config_file(
  "${CMAKE_CURRENT_SOURCE_DIR}/cmake/NestDAQConfig.cmake.in"
  "${CMAKE_CURRENT_BINARY_DIR}/cmake/NestDAQConfig.cmake"
  INSTALL_DESTINATION "${NESTDAQ_CMAKE_INSTALL_DIR}"
)

find_package(spdlog CONFIG QUIET)
if(spdlog_FOUND)
  set(_nestdaq_spdlog_check "NestDAQ_SPDLOG_USABLE_CXX${CMAKE_CXX_STANDARD}")
  unset(${_nestdaq_spdlog_check} CACHE)
  set(_nestdaq_required_libraries_saved "${CMAKE_REQUIRED_LIBRARIES}")
  set(CMAKE_REQUIRED_LIBRARIES spdlog::spdlog)
  check_cxx_source_compiles(
    "#include <spdlog/spdlog.h>
     int main() { spdlog::info(\"nestdaq spdlog probe\"); return 0; }"
    ${_nestdaq_spdlog_check}
  )
  set(CMAKE_REQUIRED_LIBRARIES "${_nestdaq_required_libraries_saved}")
  unset(_nestdaq_required_libraries_saved)

  if(${_nestdaq_spdlog_check})
    set(NestDAQ_HAVE_SPDLOG ON)
    message(STATUS "spdlog found: enabling NestDAQ spdlog helper")
  else()
    set(NestDAQ_HAVE_SPDLOG OFF)
    message(STATUS "spdlog found, but it is not usable with C++${CMAKE_CXX_STANDARD}: skipping NestDAQ spdlog helper")
  endif()
  unset(_nestdaq_spdlog_check)
else()
  set(NestDAQ_HAVE_SPDLOG OFF)
  message(STATUS "spdlog not found: skipping NestDAQ spdlog helper")
endif()

find_package(nlohmann_json CONFIG QUIET)

find_package(opentelemetry-cpp CONFIG QUIET
  COMPONENTS
    sdk
    exporters_ostream
    exporters_otlp_http
    exporters_otlp_grpc
)
if(opentelemetry-cpp_FOUND)
  message(STATUS "opentelemetry-cpp found: enabling nestdaq telemetry library")
  if(NOT nlohmann_json_FOUND)
    find_package(nlohmann_json CONFIG REQUIRED)
  endif()
  if(NestDAQ_HAVE_SPDLOG)
    set(NestDAQ_HAVE_SPDLOG_TELEMETRY ON)
    message(STATUS "spdlog found: enabling spdlog OpenTelemetry sink")
  else()
    set(NestDAQ_HAVE_SPDLOG_TELEMETRY OFF)
    message(STATUS "spdlog not found: skipping spdlog OpenTelemetry sink")
  endif()
else()
  set(NestDAQ_HAVE_SPDLOG_TELEMETRY OFF)
  message(STATUS "opentelemetry-cpp not found: skipping nestdaq telemetry library")
endif()

if(NestDAQ_BUILD_DOCS)
  find_package(Doxygen QUIET)
  find_package(Python3 QUIET COMPONENTS Interpreter)
endif()
if(NestDAQ_BUILD_DOCS AND Doxygen_FOUND AND Python3_Interpreter_FOUND)
  set(NESTDAQ_DOXYGEN_AWESOME_DEFAULT_DIR "")
  foreach(prefix IN LISTS CMAKE_PREFIX_PATH)
    set(candidate "${prefix}/${CMAKE_INSTALL_DATADIR}/doxygen-awesome-css")
    if(EXISTS "${candidate}")
      set(NESTDAQ_DOXYGEN_AWESOME_DEFAULT_DIR "${candidate}")
      break()
    endif()
    if(NOT NESTDAQ_DOXYGEN_AWESOME_DEFAULT_DIR)
      set(NESTDAQ_DOXYGEN_AWESOME_DEFAULT_DIR "${candidate}")
    endif()
  endforeach()
  if(NOT NESTDAQ_DOXYGEN_AWESOME_DEFAULT_DIR)
    set(NESTDAQ_DOXYGEN_AWESOME_DEFAULT_DIR
      "${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_DATADIR}/doxygen-awesome-css")
  endif()

  set(NESTDAQ_DOXYGEN_AWESOME_DIR
    "${NESTDAQ_DOXYGEN_AWESOME_DEFAULT_DIR}"
    CACHE PATH "Path to doxygen-awesome-css assets")
  set(NESTDAQ_DOXYGEN_AWESOME_CSS
    "${NESTDAQ_DOXYGEN_AWESOME_DIR}/doxygen-awesome.css")
  set(NESTDAQ_DOXYGEN_AWESOME_FRAGMENT_COPY_BUTTON
    "${NESTDAQ_DOXYGEN_AWESOME_DIR}/doxygen-awesome-fragment-copy-button.js")
  set(NESTDAQ_DOXYGEN_AWESOME_PARAGRAPH_LINK
    "${NESTDAQ_DOXYGEN_AWESOME_DIR}/doxygen-awesome-paragraph-link.js")
  set(NESTDAQ_DOXYGEN_AWESOME_INTERACTIVE_TOC
    "${NESTDAQ_DOXYGEN_AWESOME_DIR}/doxygen-awesome-interactive-toc.js")

  set(NESTDAQ_DOXYGEN_AWESOME_BUILD_DIR
    "${CMAKE_CURRENT_BINARY_DIR}/docs/doxygen-awesome-css")
  file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/docs/html")

  set(NESTDAQ_DOXYGEN_MERMAID_JS_URL
    "https://cdn.jsdelivr.net/npm/mermaid@11.16.1/dist/mermaid.min.js"
    CACHE STRING "Mermaid JavaScript URL used by generated Doxygen HTML")
  set(NESTDAQ_DOXYGEN_MERMAID_FILTER
    "${CMAKE_CURRENT_BINARY_DIR}/docs/doxygen-mermaid-filter.py")
  configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/DoxygenMermaidFilter.py.in"
    "${NESTDAQ_DOXYGEN_MERMAID_FILTER}"
    @ONLY
  )
  file(CHMOD "${NESTDAQ_DOXYGEN_MERMAID_FILTER}"
    PERMISSIONS
      OWNER_READ OWNER_WRITE OWNER_EXECUTE
      GROUP_READ GROUP_EXECUTE
      WORLD_READ WORLD_EXECUTE
  )

  set(NESTDAQ_DOXYGEN_AWESOME_COPY_COMMAND)
  if(EXISTS "${NESTDAQ_DOXYGEN_AWESOME_DIR}")
    list(APPEND NESTDAQ_DOXYGEN_AWESOME_COPY_COMMAND
      COMMAND
        ${CMAKE_COMMAND} -E make_directory "${NESTDAQ_DOXYGEN_AWESOME_BUILD_DIR}"
      COMMAND
        ${CMAKE_COMMAND} -E copy_directory
          "${NESTDAQ_DOXYGEN_AWESOME_DIR}"
          "${NESTDAQ_DOXYGEN_AWESOME_BUILD_DIR}")
  endif()
  set(NESTDAQ_DOXYGEN_HTML_EXTRA_STYLESHEET "")
  set(NESTDAQ_DOXYGEN_HTML_EXTRA_FILES "")
  if(EXISTS "${NESTDAQ_DOXYGEN_AWESOME_CSS}")
    set(NESTDAQ_DOXYGEN_HTML_EXTRA_STYLESHEET
      "\"../doxygen-awesome-css/doxygen-awesome.css\"")
  endif()
  foreach(extra_file
      "${NESTDAQ_DOXYGEN_AWESOME_FRAGMENT_COPY_BUTTON};doxygen-awesome-fragment-copy-button.js"
      "${NESTDAQ_DOXYGEN_AWESOME_PARAGRAPH_LINK};doxygen-awesome-paragraph-link.js"
      "${NESTDAQ_DOXYGEN_AWESOME_INTERACTIVE_TOC};doxygen-awesome-interactive-toc.js")
    list(GET extra_file 0 extra_file_source)
    list(GET extra_file 1 extra_file_name)
    if(EXISTS "${extra_file_source}")
      list(APPEND NESTDAQ_DOXYGEN_HTML_EXTRA_FILES
        "\"../doxygen-awesome-css/${extra_file_name}\"")
    endif()
  endforeach()
  if(NESTDAQ_DOXYGEN_HTML_EXTRA_FILES)
    list(JOIN NESTDAQ_DOXYGEN_HTML_EXTRA_FILES " " NESTDAQ_DOXYGEN_HTML_EXTRA_FILES)
  endif()
  if(NOT NESTDAQ_DOXYGEN_HTML_EXTRA_STYLESHEET)
    message(STATUS "doxygen-awesome-css not found under ${NESTDAQ_DOXYGEN_AWESOME_DIR}: using default Doxygen HTML style")
  endif()

  file(GLOB_RECURSE NESTDAQ_DOXYGEN_INPUTS CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/*.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/*.hh"
    "${CMAKE_CURRENT_SOURCE_DIR}/*.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/*.hxx"
    "${CMAKE_CURRENT_SOURCE_DIR}/*.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/*.cc"
    "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/*.cxx"
    "${CMAKE_CURRENT_SOURCE_DIR}/*.md"
  )

  configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/Doxyfile.in"
    "${CMAKE_CURRENT_BINARY_DIR}/Doxyfile"
    @ONLY
  )

  add_custom_command(
    OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/docs/html/index.html"
    COMMAND
      ${CMAKE_COMMAND} -E make_directory "${CMAKE_CURRENT_BINARY_DIR}/docs/html"
    ${NESTDAQ_DOXYGEN_AWESOME_COPY_COMMAND}
    COMMAND
      "${DOXYGEN_EXECUTABLE}" "${CMAKE_CURRENT_BINARY_DIR}/Doxyfile"
    COMMAND
      ${CMAKE_COMMAND}
        "-DNESTDAQ_DOXYGEN_HTML_DIR=${CMAKE_CURRENT_BINARY_DIR}/docs/html"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/PatchDoxygenAwesomeCssRefs.cmake"
    COMMAND
      ${CMAKE_COMMAND}
        "-DNESTDAQ_DOXYGEN_HTML_DIR=${CMAKE_CURRENT_BINARY_DIR}/docs/html"
        "-DNESTDAQ_DOXYGEN_MERMAID_JS_URL=${NESTDAQ_DOXYGEN_MERMAID_JS_URL}"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/InjectDoxygenMermaidScript.cmake"
    DEPENDS "${CMAKE_CURRENT_BINARY_DIR}/Doxyfile"
            "${CMAKE_CURRENT_SOURCE_DIR}/cmake/DoxygenMermaidFilter.py.in"
            "${NESTDAQ_DOXYGEN_MERMAID_FILTER}"
            "${CMAKE_CURRENT_SOURCE_DIR}/cmake/InjectDoxygenMermaidScript.cmake"
            ${NESTDAQ_DOXYGEN_INPUTS}
    WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/docs/html"
    COMMENT "Generating NestDAQ API documentation"
    VERBATIM
  )

  add_custom_target(docs ALL
    DEPENDS "${CMAKE_CURRENT_BINARY_DIR}/docs/html/index.html"
  )

  install(DIRECTORY
    "${CMAKE_CURRENT_BINARY_DIR}/docs/html/"
    DESTINATION "${CMAKE_INSTALL_DATADIR}/doc/nestdaq/html"
    PATTERN "doxygen-awesome*" EXCLUDE
  )
  if(EXISTS "${NESTDAQ_DOXYGEN_AWESOME_DIR}")
    install(DIRECTORY
      "${NESTDAQ_DOXYGEN_AWESOME_DIR}/"
      DESTINATION "${CMAKE_INSTALL_DATADIR}/doc/nestdaq/doxygen-awesome-css"
    )
  endif()
elseif(NestDAQ_BUILD_DOCS AND NOT Doxygen_FOUND)
  message(STATUS "Doxygen not found: skipping NestDAQ documentation")
elseif(NestDAQ_BUILD_DOCS)
  message(STATUS "Python 3 interpreter not found: skipping NestDAQ documentation")
else()
  message(STATUS "NestDAQ_BUILD_DOCS is OFF: skipping NestDAQ documentation")
endif()

#------------------------------------------------------------------------------
add_subdirectory(nestdaq)

if(NOT TARGET NestDAQ::NestDAQ)
  set(_nestdaq_build_tree_link_libraries
    nestdaq_telemetry
    ${Boost_LIBRARIES}
    ${fmt_LIB}
    FairLogger
  )

  if(FairMQ_VERSION VERSION_GREATER_EQUAL 1.8.0)
    list(APPEND _nestdaq_build_tree_link_libraries fairmq)
  elseif((FairMQ_VERSION VERSION_GREATER_EQUAL 1.4.55)
      AND (FairMQ_VERSION VERSION_LESS_EQUAL 1.4.56))
    list(APPEND _nestdaq_build_tree_link_libraries FairMQ)
  else()
    message(FATAL_ERROR "Unsupported FairMQ version ${FairMQ_VERSION}")
  endif()

  if(CMAKE_DL_LIBS)
    list(APPEND _nestdaq_build_tree_link_libraries ${CMAKE_DL_LIBS})
  endif()

  if(TARGET nlohmann_json::nlohmann_json)
    list(APPEND _nestdaq_build_tree_link_libraries nlohmann_json::nlohmann_json)
  endif()

  if(TARGET nestdaq_spdlog)
    list(APPEND _nestdaq_build_tree_link_libraries nestdaq_spdlog)
  endif()

  add_library(nestdaq_build_tree_interface INTERFACE)
  add_library(NestDAQ::NestDAQ ALIAS nestdaq_build_tree_interface)
  target_include_directories(nestdaq_build_tree_interface INTERFACE
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${CMAKE_CURRENT_BINARY_DIR}"
    ${Boost_INCLUDE_DIRS}
    ${FairLogger_INCDIR}
    ${FairMQ_INCDIR}
  )
  target_link_directories(nestdaq_build_tree_interface INTERFACE
    ${Boost_LIBRARY_DIRS}
    ${FairLogger_LIBDIR}
    ${FairMQ_LIBDIR}
  )
  target_link_libraries(nestdaq_build_tree_interface INTERFACE
    ${_nestdaq_build_tree_link_libraries}
  )

  unset(_nestdaq_build_tree_link_libraries)
endif()

if(BUILD_TESTING)
  add_subdirectory(tests)
endif()
add_subdirectory(plugins)
add_subdirectory(controller)
add_subdirectory(share)
add_subdirectory(scripts)
if(NestDAQ_BUILD_EXAMPLES)
  add_subdirectory(examples)
endif()
