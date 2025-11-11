if(TINYUSDZ_BUILD_EXAMPLES AND TINYUSDZ_WITH_PCP)

# PCP CLI executable
add_executable(pcp_cli
    pcp_cli.cc
)

add_sanitizers(pcp_cli)

# Link libraries
target_link_libraries(pcp_cli
    PRIVATE ${TINYUSDZ_TARGET_STATIC}
    ${CMAKE_DL_LIBS}
)

target_include_directories(pcp_cli
    PRIVATE ${PROJECT_SOURCE_DIR}/src
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
)

if(TINYUSDZ_WITH_PCP)
  target_compile_definitions(pcp_cli PRIVATE "TINYUSDZ_WITH_PCP")
endif()

if(TINYUSDZ_WITH_BLAKE3)
  target_compile_definitions(pcp_cli PRIVATE "TINYUSDZ_WITH_BLAKE3")
endif()

# Copy example files to binary directory
file(COPY examples DESTINATION ${CMAKE_CURRENT_BINARY_DIR})

# Installation
install(TARGETS pcp_cli DESTINATION bin)
install(FILES composition_schema.json DESTINATION share/pcp)
install(DIRECTORY examples DESTINATION share/pcp)

set_target_properties(pcp_cli PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}
)

endif(TINYUSDZ_BUILD_EXAMPLES AND TINYUSDZ_WITH_PCP)