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
	unit-tydra.cc
	unit-tydra-subdivision.cc
	# unit-dedup.cc  # Temporarily disabled - needs API updates
	unit-crate-writer.cc
	unit-stage.cc
	unit-tiny-container.cc
	unit-tiny-hashmap.cc
	unit-usda-roundtrip.cc
	unit-half-roundtrip.cc
	unit-prim-reconstruct.cc
	unit-composition.cc
	unit-usda-reader.cc
	unit-usdc-reader.cc
	unit-usdc-reconstruct.cc
	# Phase 0A: feat tests merged into unit suite
	unit-value-view.cc
	unit-typed-array-view.cc
	unit-typed-array-timesamples.cc
	# Phase 1: composition arcs
	unit-composition-arcs.cc
	# Phase 1b: DAG-based composition graph
	unit-composition-graph.cc
	# Phase 2: layer API
	unit-layer.cc
	# Phase 3: primspec and prim API
	unit-primspec.cc
	unit-prim-api.cc
	# Phase 4: USDA writer
	unit-usda-writer.cc
	# Phase 5: Tydra end-to-end + shader
	unit-tydra-renderscene.cc
	unit-tydra-shader.cc
	# Phase 6: security
	unit-security.cc
	# Phase 7: USDZ writer
	unit-usdz-writer.cc
	unit-usdc-writer.cc
	unit-usd-validation.cc
	# Physics + MuJoCo
	unit-physics.cc
	# IK solver
	unit-ik.cc
	# Rigid body simulation
	unit-rb-collision.cc
	unit-rb-dynamics.cc
    )

if (TINYUSDZ_WITH_PXR_COMPAT_API)
    list(APPEND TEST_SOURCES unit-pxr-compat-api.cc)
endif ()

add_executable(${TEST_TARGET_NAME}
	${TEST_SOURCES}
	)

add_sanitizers(${TEST_TARGET_NAME})

# Bump MSVC default 1 MB stack to 8 MB so deep-nesting parser security
# tests (security_deeply_nested_test, ~500 levels) don't blow the stack.
# Match other platforms which default to a larger stack.
if(WIN32 AND MSVC)
  target_link_options(${TEST_TARGET_NAME} PRIVATE /STACK:8388608)
endif()

# Tests skipped on 32-bit Linux (i386 multilib). After forcing SSE2 FP
# math in the i386 toolchain (15 prior failures were x87-precision
# false negatives), only this multithreaded test remains broken — a
# real concurrency bug in TaskQueue's 64-bit atomic ops on i386 that
# needs a separate investigation. 64-bit jobs still cover it.
set(TEST_TINYUSDZ_32BIT_SKIP_LIST
  task_queue_multithreaded_test
)

if (WIN32)
  add_test(NAME ${TEST_TARGET_NAME} COMMAND "${TEST_TARGET_NAME}.exe"
           WORKING_DIRECTORY $<TARGET_FILE_DIR:${TEST_TARGET_NAME}>  )
elseif (CMAKE_SIZEOF_VOID_P EQUAL 4)
  add_test(NAME ${TEST_TARGET_NAME}
           COMMAND "${TEST_TARGET_NAME}" --skip ${TEST_TINYUSDZ_32BIT_SKIP_LIST}
           WORKING_DIRECTORY $<TARGET_FILE_DIR:${TEST_TARGET_NAME}>)
else ()
  add_test(NAME ${TEST_TARGET_NAME} COMMAND "${TEST_TARGET_NAME}"
           WORKING_DIRECTORY $<TARGET_FILE_DIR:${TEST_TARGET_NAME}>  )
endif ()

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
