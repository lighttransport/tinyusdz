# JavaScript Scripting Example - Load USD as Layer and query LayerMetas
# Assume this cmake is called from tinyusdz root(../../)

set(EXAMPLE_TARGET "js-script")

set(JS_SCRIPT_SOURCES
    main.cc
    )

add_executable(${EXAMPLE_TARGET} ${JS_SCRIPT_SOURCES})
add_sanitizers(${EXAMPLE_TARGET})

target_include_directories(${EXAMPLE_TARGET} PRIVATE ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(${EXAMPLE_TARGET} tinyusdz_static)

# This example requires QuickJS support
if(NOT TINYUSDZ_WITH_QJS)
    message(WARNING "js-script example requires QuickJS support. Enable with -DTINYUSDZ_WITH_QJS=ON")
endif()

set_target_properties(${EXAMPLE_TARGET} PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")

# Copy JavaScript files to binary directory for easy access
configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/query_layer.js"
    "${CMAKE_BINARY_DIR}/query_layer.js"
    COPYONLY
)