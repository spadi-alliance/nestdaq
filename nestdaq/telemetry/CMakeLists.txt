install(FILES
  ${CMAKE_CURRENT_SOURCE_DIR}/FairLoggerTelemetryLoader.h
  ${CMAKE_CURRENT_SOURCE_DIR}/OpenTelemetryInitializer.h
  ${CMAKE_CURRENT_SOURCE_DIR}/Telemetry.h
  DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/nestdaq/telemetry
)

if(NestDAQ_HAVE_SPDLOG)
  install(FILES
    ${CMAKE_CURRENT_SOURCE_DIR}/SpdlogLogger.h
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/nestdaq/telemetry
  )
endif()

if(NestDAQ_HAVE_SPDLOG_TELEMETRY)
  install(FILES
    ${CMAKE_CURRENT_SOURCE_DIR}/SpdlogOpenTelemetrySink.h
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/nestdaq/telemetry
  )
endif()

add_library(nestdaq_telemetry SHARED
  FairLoggerTelemetryLoader.cxx
  Telemetry.cxx
)

target_include_directories(nestdaq_telemetry PUBLIC
  "$<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}>"
  "$<BUILD_INTERFACE:${CMAKE_BINARY_DIR}>"
  "$<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>"
)

target_include_directories(nestdaq_telemetry SYSTEM PUBLIC
  ${Boost_INCLUDE_DIRS}
  ${FairLogger_INCDIR}
  ${FairMQ_INCDIR}
)

target_link_directories(nestdaq_telemetry PUBLIC
  ${Boost_LIBRARY_DIRS}
  ${FairLogger_LIBDIR}
  ${FairMQ_LIBDIR}
)

target_link_libraries(nestdaq_telemetry PUBLIC
  ${Boost_LIBRARIES}
  FairLogger
  ${CMAKE_DL_LIBS}
)

if(FairMQ_VERSION VERSION_GREATER_EQUAL 1.8.0)
  target_link_libraries(nestdaq_telemetry PUBLIC fairmq)
elseif((FairMQ_VERSION VERSION_GREATER_EQUAL 1.4.55)
    AND (FairMQ_VERSION VERSION_LESS_EQUAL 1.4.56))
  target_link_libraries(nestdaq_telemetry PUBLIC FairMQ)
else()
  message(FATAL_ERROR "Unsupported FairMQ version ${FairMQ_VERSION}")
endif()

install(TARGETS nestdaq_telemetry
  LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
  RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
  ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
)

if(NestDAQ_HAVE_SPDLOG)
  add_library(nestdaq_spdlog SHARED
    SpdlogLogger.cxx
  )

  target_include_directories(nestdaq_spdlog PUBLIC
    "$<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}>"
    "$<BUILD_INTERFACE:${CMAKE_BINARY_DIR}>"
    "$<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>"
  )

  target_link_libraries(nestdaq_spdlog PUBLIC
    nestdaq_telemetry
    spdlog::spdlog
  )

  get_target_property(_nestdaq_spdlog_compile_definitions spdlog::spdlog INTERFACE_COMPILE_DEFINITIONS)
  if(_nestdaq_spdlog_compile_definitions
      AND ";${_nestdaq_spdlog_compile_definitions};" MATCHES ";SPDLOG_USE_STD_FORMAT;")
    target_compile_features(nestdaq_spdlog PUBLIC cxx_std_20)
  endif()
  unset(_nestdaq_spdlog_compile_definitions)

  install(TARGETS nestdaq_spdlog
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
  )
endif()

add_library(nestdaq_fairmq_throughput_parser STATIC
  FairMQThroughputLogParser.cxx
)

target_include_directories(nestdaq_fairmq_throughput_parser PUBLIC
  ${CMAKE_SOURCE_DIR}
)

set_target_properties(nestdaq_fairmq_throughput_parser PROPERTIES
  POSITION_INDEPENDENT_CODE ON
)

if(opentelemetry-cpp_FOUND)
  add_library(nestdaq_otel SHARED
    FairLoggerOpenTelemetrySink.cxx
    OpenTelemetryInitializer.cxx
    OpenTelemetryLogs.cxx
    OpenTelemetryMetrics.cxx
    OpenTelemetryRuntime.cxx
    OpenTelemetryTraces.cxx
  )

  if(NestDAQ_HAVE_SPDLOG_TELEMETRY)
    target_sources(nestdaq_otel PRIVATE
      SpdlogOpenTelemetrySink.cxx
    )
  endif()

  target_include_directories(nestdaq_otel PRIVATE
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_BINARY_DIR}
  )

  target_include_directories(nestdaq_otel SYSTEM PRIVATE
    ${FairLogger_INCDIR}
  )

  target_link_directories(nestdaq_otel PRIVATE
    ${FairLogger_LIBDIR}
  )

  target_link_libraries(nestdaq_otel PRIVATE
    nestdaq_fairmq_throughput_parser
    FairLogger
    nlohmann_json::nlohmann_json
    opentelemetry-cpp::logs
    opentelemetry-cpp::metrics
    opentelemetry-cpp::resources
    opentelemetry-cpp::trace
    opentelemetry-cpp::ostream_log_record_exporter
    opentelemetry-cpp::ostream_metrics_exporter
    opentelemetry-cpp::ostream_span_exporter
    opentelemetry-cpp::otlp_http_log_record_exporter
    opentelemetry-cpp::otlp_http_metric_exporter
    opentelemetry-cpp::otlp_http_exporter
    opentelemetry-cpp::otlp_grpc_log_record_exporter
    opentelemetry-cpp::otlp_grpc_metrics_exporter
    opentelemetry-cpp::otlp_grpc_exporter
  )

  if(NestDAQ_HAVE_SPDLOG_TELEMETRY)
    get_target_property(_nestdaq_spdlog_include_dirs spdlog::spdlog INTERFACE_INCLUDE_DIRECTORIES)
    if(_nestdaq_spdlog_include_dirs)
      target_include_directories(nestdaq_otel SYSTEM BEFORE PRIVATE
        ${_nestdaq_spdlog_include_dirs}
      )
    endif()
    unset(_nestdaq_spdlog_include_dirs)

    target_link_libraries(nestdaq_otel PRIVATE
      spdlog::spdlog
    )
    target_compile_definitions(nestdaq_otel PRIVATE
      NESTDAQ_HAVE_SPDLOG=1
    )
  endif()

  set_target_properties(nestdaq_otel PROPERTIES
    OUTPUT_NAME nestdaq_otel
  )

  install(TARGETS nestdaq_otel
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
  )
endif()
