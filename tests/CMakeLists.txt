find_package(Catch2 CONFIG REQUIRED)
include(Catch)

add_executable(nestdaq_telemetry_options_tests
  fairmq_throughput_log_parser_tests.cxx
  telemetry_options_tests.cxx
)

target_include_directories(nestdaq_telemetry_options_tests PRIVATE
  ${CMAKE_SOURCE_DIR}
  ${CMAKE_BINARY_DIR}
  ${FairLogger_INCDIR}
)

target_link_libraries(nestdaq_telemetry_options_tests PRIVATE
  nestdaq_telemetry
  nestdaq_fairmq_throughput_parser
  Catch2::Catch2WithMain
  FairMQ::FairMQ
  Boost::program_options
)

catch_discover_tests(nestdaq_telemetry_options_tests)

if(opentelemetry-cpp_FOUND)
  add_executable(nestdaq_telemetry_library_tests
    telemetry_library_tests.cxx
  )

  target_include_directories(nestdaq_telemetry_library_tests PRIVATE
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_BINARY_DIR}
    ${FairLogger_INCDIR}
  )

  target_compile_definitions(nestdaq_telemetry_library_tests PRIVATE
    NESTDAQ_OTEL_LIBRARY_PATH="$<TARGET_FILE:nestdaq_otel>"
  )

  if(NestDAQ_HAVE_SPDLOG_TELEMETRY)
    target_compile_definitions(nestdaq_telemetry_library_tests PRIVATE
      NESTDAQ_HAVE_SPDLOG=1
    )
  else()
    target_compile_definitions(nestdaq_telemetry_library_tests PRIVATE
      NESTDAQ_HAVE_SPDLOG=0
    )
  endif()

  target_link_libraries(nestdaq_telemetry_library_tests PRIVATE
    nestdaq_telemetry
    Catch2::Catch2WithMain
    FairMQ::FairMQ
    Boost::program_options
    nlohmann_json::nlohmann_json
    ${CMAKE_DL_LIBS}
  )

  if(NestDAQ_HAVE_SPDLOG_TELEMETRY)
    get_target_property(_nestdaq_spdlog_include_dirs spdlog::spdlog INTERFACE_INCLUDE_DIRECTORIES)
    if(_nestdaq_spdlog_include_dirs)
      target_include_directories(nestdaq_telemetry_library_tests SYSTEM BEFORE PRIVATE
        ${_nestdaq_spdlog_include_dirs}
      )
    endif()
    unset(_nestdaq_spdlog_include_dirs)

    target_link_libraries(nestdaq_telemetry_library_tests PRIVATE
      nestdaq_spdlog
      nestdaq_otel
      spdlog::spdlog
    )
  endif()

  add_dependencies(nestdaq_telemetry_library_tests nestdaq_otel)
  catch_discover_tests(nestdaq_telemetry_library_tests)
endif()
