# Assume this cmake is called from lightusd root(../../)
set(EXAMPLE_TARGET "tusdcat")

set(LIGHTUSD_SIMPLE_DUMP_SOURCES
    main.cc
    variant-format.cc
    comp-graph-dump.cc
    )

add_executable(${EXAMPLE_TARGET} ${LIGHTUSD_SIMPLE_DUMP_SOURCES})
add_sanitizers(${EXAMPLE_TARGET})

target_include_directories(${EXAMPLE_TARGET} PRIVATE ${PROJECT_SOURCE_DIR}/src)
# Allocator (if enabled) must precede the rest so it wins symbol resolution.
target_link_libraries(${EXAMPLE_TARGET} ${LIGHTUSD_TOOLS_ALLOCATOR_LIB} lightusd_static)


set_target_properties(${EXAMPLE_TARGET} PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")
