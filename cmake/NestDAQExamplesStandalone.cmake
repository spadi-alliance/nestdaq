include_guard(GLOBAL)

if(CMAKE_CURRENT_SOURCE_DIR STREQUAL CMAKE_SOURCE_DIR)
  include(GNUInstallDirs)
  include("${CMAKE_CURRENT_LIST_DIR}/NestDAQBuildSettings.cmake")

  set(CMAKE_CXX_STANDARD_REQUIRED ON)
  if(NOT CMAKE_CXX_STANDARD)
    set(CMAKE_CXX_STANDARD 17)
  elseif(CMAKE_CXX_STANDARD LESS 17)
    message(FATAL_ERROR "A minimum CMAKE_CXX_STANDARD of 17 is required for NestDAQ examples.")
  endif()
  set(CMAKE_CXX_EXTENSIONS OFF)

  set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
  nestdaq_enable_clang_tidy("Run clang-tidy during builds")
  nestdaq_configure_install_rpath()
endif()
