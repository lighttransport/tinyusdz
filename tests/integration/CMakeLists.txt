# PCP Integration Examples and Tests

if(TINYUSDZ_BUILD_TESTS AND TINYUSDZ_WITH_PCP)

  set(TEST_TARGET_NAME pcp_integration_examples)

  add_executable(${TEST_TARGET_NAME}
    pcp-integration-examples.cc
  )

  add_sanitizers(${TEST_TARGET_NAME})

  target_link_libraries(${TEST_TARGET_NAME}
    PRIVATE ${TINYUSDZ_TARGET_STATIC}
    ${CMAKE_DL_LIBS}
  )

  target_include_directories(${TEST_TARGET_NAME}
    PRIVATE ${PROJECT_SOURCE_DIR}/src
  )

  if(TINYUSDZ_WITH_PCP)
    target_compile_definitions(${TEST_TARGET_NAME} PRIVATE "TINYUSDZ_WITH_PCP")
  endif()

  if(TINYUSDZ_WITH_BLAKE3)
    target_compile_definitions(${TEST_TARGET_NAME} PRIVATE "TINYUSDZ_WITH_BLAKE3")
  endif()

  set_target_properties(${TEST_TARGET_NAME} PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}
  )

  if (WIN32)
    add_test(NAME ${TEST_TARGET_NAME}
      COMMAND "${TEST_TARGET_NAME}.exe"
      WORKING_DIRECTORY $<TARGET_FILE_DIR:${TEST_TARGET_NAME}>
    )
  else (WIN32)
    add_test(NAME ${TEST_TARGET_NAME}
      COMMAND "${TEST_TARGET_NAME}"
      WORKING_DIRECTORY $<TARGET_FILE_DIR:${TEST_TARGET_NAME}>
    )
  endif (WIN32)

endif(TINYUSDZ_BUILD_TESTS AND TINYUSDZ_WITH_PCP)
