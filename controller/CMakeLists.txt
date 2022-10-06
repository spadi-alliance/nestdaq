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
  ${Boost_INCLUDE_DIRS};
  ${FairMQ_INCDIR};
  ${HIREDIS_HEADER};
  ${REDIS_PLUS_PLUS_HEADER};
  ${CMAKE_SOURCE_DIR};
  ${CMAKE_BINARY_DIR};
)

target_link_directories(${EXEC} PUBLIC
  ${Boost_LIBRARY_DIRS};
  ${FairMQ_LIBDIR};
)

target_link_libraries(${EXEC} PUBLIC
  ${Boost_LIBRARIES};
  FairMQStateMachine;
  ${HIREDIS_LIB};
  ${REDIS_PLUS_PLUS_LIB};
  ${CMAKE_THREAD_LIBS_INIT};
)

install(TARGETS
  ${EXEC};
  ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
  LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
  RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)