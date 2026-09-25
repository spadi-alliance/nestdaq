if(NOT DEFINED NESTDAQ_DOXYGEN_HTML_DIR)
  message(FATAL_ERROR "NESTDAQ_DOXYGEN_HTML_DIR is not set")
endif()
if(NOT DEFINED NESTDAQ_DOXYGEN_MERMAID_JS_URL)
  message(FATAL_ERROR "NESTDAQ_DOXYGEN_MERMAID_JS_URL is not set")
endif()

file(GLOB html_files "${NESTDAQ_DOXYGEN_HTML_DIR}/*.html")
foreach(html_file IN LISTS html_files)
  file(READ "${html_file}" html_content)
  if(html_content MATCHES "<pre class=\"mermaid\">")
    set(start_marker "<!-- nestdaq-mermaid:start -->")
    set(end_marker "<!-- nestdaq-mermaid:end -->")
    set(mermaid_script
      "<script src=\"${NESTDAQ_DOXYGEN_MERMAID_JS_URL}\"></script>\n<script>mermaid.initialize({startOnLoad: true, securityLevel: \"strict\"});</script>")

    while(1)
      string(FIND "${html_content}" "${start_marker}" start_position)
      if(start_position EQUAL -1)
        break()
      endif()
      string(FIND "${html_content}" "${end_marker}" end_position)
      if(end_position EQUAL -1)
        message(FATAL_ERROR "Unterminated Mermaid marker in ${html_file}")
      endif()
      string(LENGTH "${end_marker}" end_marker_length)
      math(EXPR suffix_position "${end_position} + ${end_marker_length}")
      string(LENGTH "${html_content}" html_length)
      math(EXPR suffix_length "${html_length} - ${suffix_position}")
      string(SUBSTRING "${html_content}" 0 ${start_position} prefix)
      string(SUBSTRING
        "${html_content}" ${suffix_position} ${suffix_length} suffix)
      set(html_content "${prefix}${suffix}")
    endwhile()

    string(REPLACE
      "<script type=\"module\" src=\"nestdaq-mermaid.js\"></script>\n"
      ""
      html_content "${html_content}")
    string(REPLACE
      "${mermaid_script}\n"
      ""
      html_content "${html_content}")
    string(REPLACE
      "</body>"
      "${start_marker}\n${mermaid_script}\n${end_marker}\n</body>"
      html_content "${html_content}")
    file(WRITE "${html_file}" "${html_content}")
  endif()
endforeach()
