install(FILES
  README.md
  README.ja.md
  DESTINATION ${CMAKE_INSTALL_DATADIR}/installers
)

install(PROGRAMS
  install-opensearch-dashboards.sh
  install-opensearch.sh
  install-otelcol-contrib.sh
  install-redis-stack.sh
  DESTINATION ${CMAKE_INSTALL_DATADIR}/installers
)
