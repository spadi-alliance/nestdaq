#==============================================================================
foreach(lvar IN ITEMS Sampler; Sink; NullDevice;)
  set(EXEC ${lvar})
  add_executable(${EXEC} 
    ${EXEC}.cxx;
  )

  target_include_directories(${EXEC} PUBLIC 
    ${Boost_INCLUDE_DIRS};  
    ${FairLogger_INCDIR};
    ${FairMQ_INCDIR};
    ${CMAKE_SOURCE_DIR};
  )

  target_link_directories(${EXEC} PUBLIC
    ${Boost_LIBRARY_DIRS}; 
    ${FairLogger_LIBDIR};
    ${FairMQ_LIBDIR};
  )

  target_link_libraries(${EXEC} PUBLIC 
    ${Boost_LIBRARIES};  
    ${fmt_LIB};
    FairLogger;
    FairMQ;
  )

  install(TARGETS
    ${EXEC};
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
  )
endforeach()