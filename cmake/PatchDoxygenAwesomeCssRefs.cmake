if(NOT DEFINED NESTDAQ_DOXYGEN_HTML_DIR)
  message(FATAL_ERROR "NESTDAQ_DOXYGEN_HTML_DIR is not set")
endif()

file(GLOB html_files "${NESTDAQ_DOXYGEN_HTML_DIR}/*.html")
foreach(html_file IN LISTS html_files)
  file(READ "${html_file}" html_content)
  string(REPLACE
    "href=\"doxygen-awesome.css\""
    "href=\"../doxygen-awesome-css/doxygen-awesome.css\""
    html_content "${html_content}")
  file(WRITE "${html_file}" "${html_content}")
endforeach()
