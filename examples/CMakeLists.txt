cmake_minimum_required(VERSION 3.22)

set(_nestdaq_examples_standalone FALSE)
if(CMAKE_CURRENT_SOURCE_DIR STREQUAL CMAKE_SOURCE_DIR)
  # Normal examples workflow: configure examples/ as an independent project
  # against an installed NestDAQ package.
  set(_nestdaq_examples_standalone TRUE)
  project(NestDAQExamples LANGUAGES CXX)
else()
  # Special case: examples/ is included from the NestDAQ top-level project via
  # add_subdirectory(), so the parent project already defines NestDAQ targets.
endif()

include("${CMAKE_CURRENT_LIST_DIR}/../cmake/NestDAQExamplesStandalone.cmake")

# Standalone examples could unconditionally call find_package(NestDAQ).
# This guard is needed because examples/ can also be added from the NestDAQ
# top-level project, where the parent already defines NestDAQ::NestDAQ and
# there is no installed package to find. Check the target, not NestDAQ_FOUND:
# *_FOUND reflects find_package() results, while the subdirectory case only
# needs to know whether the link target already exists.
if(NOT TARGET NestDAQ::NestDAQ)
  find_package(NestDAQ REQUIRED CONFIG)
endif()

# Detect whether NestDAQ::NestDAQ already carries the NestDAQ spdlog helper.
# In that case examples should not add another spdlog link path: the helper and
# its spdlog dependency are already part of the NestDAQ interface. If the helper
# is absent, examples may still use an independently installed upstream
# spdlog::spdlog target as a console-only fallback.
set(_nestdaq_examples_have_spdlog_helper FALSE)
get_target_property(_nestdaq_examples_nestdaq_links NestDAQ::NestDAQ INTERFACE_LINK_LIBRARIES)
foreach(_nestdaq_examples_nestdaq_link IN LISTS _nestdaq_examples_nestdaq_links)
  if(_nestdaq_examples_nestdaq_link STREQUAL "nestdaq_spdlog"
      OR _nestdaq_examples_nestdaq_link MATCHES "/libnestdaq_spdlog\\.")
    set(_nestdaq_examples_have_spdlog_helper TRUE)
  endif()
endforeach()
unset(_nestdaq_examples_nestdaq_link)
unset(_nestdaq_examples_nestdaq_links)

# If NestDAQ was built with spdlog support, NestDAQ::NestDAQ already carries
# the NestDAQ spdlog helper and its spdlog dependency. If not, standalone
# examples may still use an independently installed upstream spdlog package.
if(NOT _nestdaq_examples_have_spdlog_helper AND NOT TARGET spdlog::spdlog)
  find_package(spdlog CONFIG QUIET)
endif()

foreach(exec IN ITEMS Sampler Sink NullDevice)
  add_executable(${exec}
    ${exec}.cxx
  )

  target_link_libraries(${exec} PUBLIC
    NestDAQ::NestDAQ
  )

  if(NOT _nestdaq_examples_have_spdlog_helper AND TARGET spdlog::spdlog)
    target_link_libraries(${exec} PRIVATE
      spdlog::spdlog
    )
  endif()

  install(TARGETS ${exec}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
  )
endforeach()

unset(_nestdaq_examples_have_spdlog_helper)
unset(_nestdaq_examples_standalone)
