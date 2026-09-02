message(STATUS "========== include doxygen-awesome-css.cmake ==========")

include(ExternalProject)

set(doxygen-awesome-css_RELEASE_TAG "v${doxygen-awesome-css_VERSION}")
set(doxygen-awesome-css_RELEASE_URL
  "https://github.com/jothepro/doxygen-awesome-css/archive/refs/tags/${doxygen-awesome-css_RELEASE_TAG}.tar.gz")

message(STATUS "Installing doxygen-awesome-css from ${doxygen-awesome-css_RELEASE_URL}")

ExternalProject_Add(
  doxygen-awesome-css
  URL ${doxygen-awesome-css_RELEASE_URL}
  ${NESTDAQ_DOWNLOAD_EXTRACT_TIMESTAMP_ARGS}
  UPDATE_COMMAND ""
  CONFIGURE_COMMAND ""
  BUILD_COMMAND ""
  INSTALL_COMMAND
    ${CMAKE_COMMAND} -E make_directory
      <INSTALL_DIR>/${CMAKE_INSTALL_DATADIR}/doxygen-awesome-css
    COMMAND
      ${CMAKE_COMMAND} -E copy_directory
      <SOURCE_DIR>
      <INSTALL_DIR>/${CMAKE_INSTALL_DATADIR}/doxygen-awesome-css
  INSTALL_DIR ${CMAKE_INSTALL_PREFIX}
)

message(STATUS "========== include doxygen-awesome-css.cmake done ==========")
