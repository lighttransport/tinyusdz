set(TEST_TARGET_NAME unit-test-tinyusdz)

set(TEST_SOURCES
	unit-main.cc

	unit-ascii-parse.cc
	unit-customdata.cc
	unit-handle-allocator.cc
	unit-prim-types.cc
	unit-primvar.cc
	unit-pathutil.cc
	unit-pprint.cc
	unit-strutil.cc
	unit-value-types.cc
	unit-xform.cc
	unit-math.cc
	unit-ioutil.cc
	unit-timesamples.cc
	unit-fp-parse-print.cc
	unit-materialx.cc
	unit-task-queue.cc
	# unit-dedup.cc  # Temporarily disabled - needs API updates
	unit-crate-writer.cc
	unit-stage.cc
	unit-tiny-container.cc
	unit-usda-roundtrip.cc
   )

if (TINYUSDZ_WITH_PXR_COMPAT_API)
    list(APPEND TEST_SOURCES unit-pxr-compat-api.cc)
endif ()

add_executable(${TEST_TARGET_NAME}
	${TEST_SOURCES}
	)

add_sanitizers(${TEST_TARGET_NAME})

if (WIN32)
  add_test(NAME ${TEST_TARGET_NAME} COMMAND "${TEST_TARGET_NAME}.exe"
           WORKING_DIRECTORY $<TARGET_FILE_DIR:${TEST_TARGET_NAME}>  )
else (WIN32)
  add_test(NAME ${TEST_TARGET_NAME} COMMAND "${TEST_TARGET_NAME}"
           WORKING_DIRECTORY $<TARGET_FILE_DIR:${TEST_TARGET_NAME}>  )
endif (WIN32)

target_link_libraries(${TEST_TARGET_NAME} PRIVATE tinyusdz_static ${CMAKE_DL_LIBS})
target_include_directories(${TEST_TARGET_NAME} PRIVATE ${PROJECT_SOURCE_DIR}/src)

# Add crate-writer library and includes for dedup tests
if(TARGET crate-writer)
  target_link_libraries(${TEST_TARGET_NAME} PRIVATE crate-writer)
  target_include_directories(${TEST_TARGET_NAME} PRIVATE ${PROJECT_SOURCE_DIR}/sandbox/crate-writer/include)
endif()

set_target_properties(${TEST_TARGET_NAME} PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})

if (TINYUSDZ_WITH_PXR_COMPAT_API)
  target_compile_definitions(${TEST_TARGET_NAME} PRIVATE "TINYUSDZ_WITH_PXR_COMPAT_API")

  # FIXME: support dll build
  target_compile_definitions(${TEST_TARGET_NAME} PRIVATE "PXR_STATIC")
endif ()


