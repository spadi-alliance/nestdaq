set(EX_DATADIR "${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_DATADIR}")
Set(DefaultPathFiles
  DaqWebControlDefaultDocRootPath.h;
)
foreach(lvar IN ITEMS ${DefaultPathFiles})
  configure_file(${lvar}.in ${lvar} @ONLY)
endforeach()

# --------- add hiredis dependency ----------
find_path(HIREDIS_HEADER hiredis)
find_library(HIREDIS_LIB hiredis)

# --------- add redis++ dependency ----------
find_path(REDIS_PLUS_PLUS_HEADER sw)
find_path(REDIS_PLUS_PLUS_LIB redis++)

# ===============================================
# web gui using websocket
# ===============================================
set(EXEC daq-webctl)
add_executable(${EXEC}
  run_${EXEC}.cxx;
  WebGui.cxx;
  beast_tools.cxx;
  websocket_session.cxx;
  http_session.cxx;
  listener.cxx;
  HttpWebSocketServer.cxx;
  ${CMAKE_SOURCE_DIR}/plugins/tools.cxx;
)

target_include_directories(${EXEC} PUBLIC
  ${CMAKE_SOURCE_DIR};
  ${CMAKE_BINARY_DIR};
)

target_include_directories(${EXEC} SYSTEM PUBLIC
  ${Boost_INCLUDE_DIRS};
  ${FairLogger_INCDIR};
  ${FairMQ_INCDIR};
  ${HIREDIS_HEADER};
  ${REDIS_PLUS_PLUS_HEADER};
)

target_link_directories(${EXEC} PUBLIC
  ${Boost_LIBRARY_DIRS};
  ${FairLogger_LIBDIR};
  ${FairMQ_LIBDIR};
)

if (FairMQ_VERSION VERSION_GREATER_EQUAL 1.8.0)
  target_link_libraries(${EXEC} PUBLIC
    ${Boost_LIBRARIES};
    nestdaq_telemetry;
    fairmq;
    FairLogger;
    ${fmt_LIB};
    ${HIREDIS_LIB};
    ${REDIS_PLUS_PLUS_LIB};
    ${CMAKE_THREAD_LIBS_INIT};
    ${CMAKE_DL_LIBS};
  )
elseif ((FairMQ_VERSION VERSION_GREATER_EQUAL 1.4.55)
    AND (FairMQ_VERSION VERSION_LESS_EQUAL 1.4.56))
  target_link_libraries(${EXEC} PUBLIC
    ${Boost_LIBRARIES};
    nestdaq_telemetry;
    FairMQStateMachine;
    FairLogger;
    ${fmt_LIB};
    ${HIREDIS_LIB};
    ${REDIS_PLUS_PLUS_LIB};
    ${CMAKE_THREAD_LIBS_INIT};
    ${CMAKE_DL_LIBS};
  )
else()
  message(FATAL_ERROR "Unsupported FairMQ version ${FairMQ_VERSION}")
endif()

install(TARGETS
  ${EXEC};
  ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
  LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
  RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)
