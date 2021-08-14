set(TEST_TARGET_NAME unit-test-tinyusdz)

add_executable(${TEST_TARGET_NAME}
	unit-main.cc
	unit-prim-types.cc
	)
add_sanitizers(${TEST_TARGET_NAME})

if (WIN32)
  add_test(NAME ${TEST_TARGET_NAME} COMMAND "${TEST_TARGET_NAME}.exe"
           WORKING_DIRECTORY $<TARGET_FILE_DIR:${TEST_TARGET_NAME}>  )
else (WIN32)
  add_test(NAME ${TEST_TARGET_NAME} COMMAND "${TEST_TARGET_NAME}"
           WORKING_DIRECTORY $<TARGET_FILE_DIR:${TEST_TARGET_NAME}>  )
endif (WIN32)

target_link_libraries(${TEST_TARGET_NAME} PRIVATE tinyusdz ${CMAKE_DL_LIBS})
target_include_directories(${TEST_TARGET_NAME} PRIVATE ${PROJECT_SOURCE_DIR}/src)

set_target_properties(${TEST_TARGET_NAME} PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})

