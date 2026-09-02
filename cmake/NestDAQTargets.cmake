include_guard(GLOBAL)

include(CMakeFindDependencyMacro)
find_dependency(Threads)
find_package(spdlog CONFIG QUIET)
find_package(nlohmann_json CONFIG QUIET)

if(NOT TARGET NestDAQ::NestDAQ)
  add_library(NestDAQ::NestDAQ INTERFACE IMPORTED)

  set(_nestdaq_include_dir "${PACKAGE_PREFIX_DIR}/include")
  if(NOT EXISTS "${_nestdaq_include_dir}/nestdaq/runDevice.h")
    message(FATAL_ERROR "NestDAQ include directory does not contain nestdaq/runDevice.h: ${_nestdaq_include_dir}")
  endif()

  find_library(_nestdaq_telemetry_library
    NAMES nestdaq_telemetry
    PATHS
      "${PACKAGE_PREFIX_DIR}/lib64"
      "${PACKAGE_PREFIX_DIR}/lib"
    NO_DEFAULT_PATH
  )
  if(NOT _nestdaq_telemetry_library)
    message(FATAL_ERROR "NestDAQ telemetry library was not found under ${PACKAGE_PREFIX_DIR}")
  endif()

  set(_nestdaq_link_libraries
    ${_nestdaq_telemetry_library}
    ${Boost_LIBRARIES}
    ${fmt_LIB}
    FairLogger
  )

  if(FairMQ_VERSION VERSION_GREATER_EQUAL 1.8.0)
    list(APPEND _nestdaq_link_libraries fairmq)
  elseif((FairMQ_VERSION VERSION_GREATER_EQUAL 1.4.55)
      AND (FairMQ_VERSION VERSION_LESS_EQUAL 1.4.56))
    list(APPEND _nestdaq_link_libraries FairMQ)
  else()
    message(FATAL_ERROR "Unsupported FairMQ version ${FairMQ_VERSION}")
  endif()

  if(CMAKE_DL_LIBS)
    list(APPEND _nestdaq_link_libraries ${CMAKE_DL_LIBS})
  endif()

  if(TARGET nlohmann_json::nlohmann_json)
    list(APPEND _nestdaq_link_libraries nlohmann_json::nlohmann_json)
  endif()

  set(_nestdaq_compile_features)
  if(TARGET spdlog::spdlog)
    find_library(_nestdaq_spdlog_library
      NAMES nestdaq_spdlog
      PATHS
        "${PACKAGE_PREFIX_DIR}/lib64"
        "${PACKAGE_PREFIX_DIR}/lib"
      NO_DEFAULT_PATH
    )
    if(_nestdaq_spdlog_library AND EXISTS "${_nestdaq_include_dir}/nestdaq/telemetry/SpdlogLogger.h")
      list(APPEND _nestdaq_link_libraries
        ${_nestdaq_spdlog_library}
        spdlog::spdlog
      )

      get_target_property(_nestdaq_spdlog_compile_definitions spdlog::spdlog INTERFACE_COMPILE_DEFINITIONS)
      if(_nestdaq_spdlog_compile_definitions
          AND ";${_nestdaq_spdlog_compile_definitions};" MATCHES ";SPDLOG_USE_STD_FORMAT;")
        list(APPEND _nestdaq_compile_features cxx_std_20)
      endif()
      unset(_nestdaq_spdlog_compile_definitions)
    endif()
  endif()

  set_target_properties(NestDAQ::NestDAQ PROPERTIES
    INTERFACE_INCLUDE_DIRECTORIES "${_nestdaq_include_dir};${Boost_INCLUDE_DIRS};${FairLogger_INCDIR};${FairMQ_INCDIR}"
    INTERFACE_COMPILE_FEATURES "${_nestdaq_compile_features}"
    INTERFACE_LINK_DIRECTORIES "${Boost_LIBRARY_DIRS};${FairLogger_LIBDIR};${FairMQ_LIBDIR}"
    INTERFACE_LINK_LIBRARIES "${_nestdaq_link_libraries}"
  )

  unset(_nestdaq_include_dir)
  unset(_nestdaq_compile_features)
  unset(_nestdaq_telemetry_library)
  unset(_nestdaq_spdlog_library)
  unset(_nestdaq_link_libraries)
endif()
