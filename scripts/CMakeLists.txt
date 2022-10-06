set(EX_BINDIR "${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_BINDIR}")
set(EX_LIBDIR "${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_LIBDIR}")
message(STATUS "EX_BINDIR = ${EX_BINDIR}")
message(STATUS "EX_LIBDIR = ${EX_LIBDIR}")
configure_file(${CMAKE_CURRENT_SOURCE_DIR}/start_device.sh.in ${CMAKE_CURRENT_BINARY_DIR}/start_device.sh @ONLY)


install(PROGRAMS
  ${CMAKE_CURRENT_BINARY_DIR}/start_device.sh;
  mq-param.sh;
  topology-1-1.sh;
  topology-n-n-m.sh;
  DESTINATION ${CMAKE_INSTALL_PREFIX}/scripts
)
