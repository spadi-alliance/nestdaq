set(ORIGINAL  ${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_DATADIR}/controller/daq-webctl.html)
set(LINK  ${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_DATADIR}/controller/index.html)

install(FILES
  daq-webctl.html;
  DESTINATION ${CMAKE_INSTALL_DATADIR}/controller
)

install(CODE "execute_process(\
  COMMAND ${CMAKE_COMMAND} -E create_symlink \
  ${ORIGINAL} \
  ${LINK} \
  )"
)