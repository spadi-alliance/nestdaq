# ---------- add hiredis dependency ----------
find_path(HIREDIS_HEADER hiredis)
find_library(HIREDIS_LIB hiredis)

message(STATUS "HIREDIS_HEADER: ${HIREDIS_HEADER}")
message(STATUS "HIREDIS_LIB:    ${HIREDIS_LIB}")

# ---------- add redis++ dependency ---------
find_path(REDIS_PLUS_PLUS_HEADER sw)
find_library(REDIS_PLUS_PLUS_LIB redis++)

message(STATUS "REDIS_PLUS_PLUS_HEADER: ${REDIS_PLUS_PLUS_HEADER}")
message(STATUS "REDIS_PLUS_PLUS_LIB:    ${REDIS_PLUS_PLUS_LIB}")

#==============================================================================
# DAQ service plugin
set(PLUGIN FairMQPlugin_daq_service)
add_library(${PLUGIN} SHARED 
  DaqServicePlugin.cxx;
  Timer.cxx;
  TopologyConfig.cxx;
  TimeUtil.cxx;
  tools.cxx;
)

target_include_directories(${PLUGIN} PRIVATE 
  ${Boost_INCLUDE_DIRS};
  ${FairLogger_INCDIR};
  ${FairMQ_INCDIR};
  ${HIREDIS_HEADER};
  ${REDIS_PLUS_PLUS_HEADER};
  ${CMAKE_SOURCE_DIR};
)

target_link_libraries(${PLUGIN}
  ${CMAKE_THREAD_LIBS_INIT};
  ${HIREDIS_LIB};
  ${REDIS_PLUS_PLUS_LIB};
)

set_target_properties(${PLUGIN} PROPERTIES CXX_VISIBILITY_PRESET hidden)


#==============================================================================
# Metrics plugin
set(PLUGIN FairMQPlugin_metrics)
add_library(${PLUGIN} SHARED 
  MetricsPlugin.cxx;
  Timer.cxx;
  TimeUtil.cxx;
)

target_include_directories(${PLUGIN} PRIVATE 
  ${Boost_INCLUDE_DIRS};
  ${FairLogger_INCDIR};
  ${FairMQ_INCDIR};
  ${HIREDIS_HEADER};
  ${REDIS_PLUS_PLUS_HEADER};
  ${CMAKE_SOURCE_DIR};
)

target_link_libraries(${PLUGIN}
  ${CMAKE_THREAD_LIBS_INIT};
  ${HIREDIS_LIB};
  ${REDIS_PLUS_PLUS_LIB};
)

set_target_properties(${PLUGIN} PROPERTIES CXX_VISIBILITY_PRESET hidden)

#==============================================================================
# ParameterConfig plugin
set(PLUGIN FairMQPlugin_parameter_config)
add_library(${PLUGIN} SHARED 
  ParameterConfigPlugin.cxx;
)

target_include_directories(${PLUGIN} PRIVATE 
  ${Boost_INCLUDE_DIRS};
  ${FairLogger_INCDIR};
  ${FairMQ_INCDIR};
  ${HIREDIS_HEADER};
  ${REDIS_PLUS_PLUS_HEADER};
  ${CMAKE_SOURCE_DIR};
)

target_link_libraries(${PLUGIN}
  ${CMAKE_THREAD_LIBS_INIT};
  ${HIREDIS_LIB};
  ${REDIS_PLUS_PLUS_LIB};
)

set_target_properties(${PLUGIN} PROPERTIES CXX_VISIBILITY_PRESET hidden)

#==============================================================================
# Telemetry plugin
set(PLUGIN FairMQPlugin_telemetry)
add_library(${PLUGIN} SHARED
  TelemetryPlugin.cxx;
  tools.cxx;
)

target_include_directories(${PLUGIN} PRIVATE
  ${Boost_INCLUDE_DIRS};
  ${FairLogger_INCDIR};
  ${FairMQ_INCDIR};
  ${CMAKE_SOURCE_DIR};
)

target_link_libraries(${PLUGIN}
  ${CMAKE_THREAD_LIBS_INIT};
)

set_target_properties(${PLUGIN} PROPERTIES CXX_VISIBILITY_PRESET hidden)

#==============================================================================
install(TARGETS
  FairMQPlugin_daq_service; 
  FairMQPlugin_metrics;
  FairMQPlugin_parameter_config;
  FairMQPlugin_telemetry;
  LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
  RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)
