install(FILES
  README.md
  README.ja.md
  DESTINATION ${CMAKE_INSTALL_DATADIR}/redis-stack-container
)

install(PROGRAMS
  run-redis-7.2-stack.sh
  run-redis-7.2-stack-server.sh
  run-redis-8.2.7.sh
  run-redis-stack.sh
  run-redis-stack-server.sh
  DESTINATION ${CMAKE_INSTALL_DATADIR}/redis-stack-container
)
