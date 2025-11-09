cmake_minimum_required(VERSION 3.12)
project(USDCrateExamples CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Option to use monolithic build
option(USE_MONOLITHIC_USD "Use monolithic USD library (libusd_ms)" OFF)

# Find USD installation
# Set USD_ROOT to your OpenUSD installation directory
if(NOT DEFINED USD_ROOT)
    if(USE_MONOLITHIC_USD)
        set(USD_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../dist_nopython_monolithic"
            CACHE PATH "USD installation directory (monolithic)")
    else()
        set(USD_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../dist_nopython"
            CACHE PATH "USD installation directory")
    endif()
endif()

message(STATUS "USD_ROOT: ${USD_ROOT}")
message(STATUS "USE_MONOLITHIC_USD: ${USE_MONOLITHIC_USD}")

# Check if USD installation exists
if(NOT EXISTS "${USD_ROOT}")
    message(FATAL_ERROR "USD installation not found at: ${USD_ROOT}\n"
                        "Please build OpenUSD first or set USD_ROOT")
endif()

# USD include directories
include_directories(${USD_ROOT}/include)

# USD library directory
link_directories(${USD_ROOT}/lib)

# Platform-specific settings
if(UNIX AND NOT APPLE)
    # Linux
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wno-deprecated")
    # Add rpath for runtime library loading
    set(CMAKE_INSTALL_RPATH "${USD_ROOT}/lib")
    set(CMAKE_BUILD_RPATH "${USD_ROOT}/lib")
endif()

# Define USD libraries based on build type
if(USE_MONOLITHIC_USD)
    # Monolithic build - single library
    set(USD_LIBRARIES
        usd_ms
        tbb
    )
    message(STATUS "Using monolithic USD library: libusd_ms")
else()
    # Standard build - multiple libraries
    set(USD_LIBRARIES
        # Core USD libraries
        usd
        usdGeom
        sdf
        tf
        vt
        gf
        ar
        arch
        plug
        trace
        work
        # TBB for threading
        tbb
    )
    message(STATUS "Using standard USD libraries")
endif()

# Print found libraries
message(STATUS "USD libraries: ${USD_LIBRARIES}")

# Common function to create executable
function(add_usd_executable target source)
    add_executable(${target} ${source})

    target_link_libraries(${target}
        ${USD_LIBRARIES}
        ${CMAKE_DL_LIBS}
        pthread
    )

    # Set RPATH
    set_target_properties(${target} PROPERTIES
        BUILD_RPATH "${USD_ROOT}/lib"
        INSTALL_RPATH "${USD_ROOT}/lib"
    )

    message(STATUS "Added executable: ${target}")
endfunction()

# Build executables
add_usd_executable(crate_reader src/crate_reader.cpp)
add_usd_executable(crate_writer src/crate_writer.cpp)
add_usd_executable(crate_internal_api src/crate_internal_api.cpp)

# Install targets
install(TARGETS crate_reader crate_writer crate_internal_api
        RUNTIME DESTINATION bin)

# Print summary
message(STATUS "")
message(STATUS "========================================")
message(STATUS "USD Crate Examples Configuration")
message(STATUS "========================================")
message(STATUS "  Build type: ${CMAKE_BUILD_TYPE}")
message(STATUS "  USD root: ${USD_ROOT}")
message(STATUS "  Monolithic: ${USE_MONOLITHIC_USD}")
message(STATUS "  Compiler: ${CMAKE_CXX_COMPILER}")
message(STATUS "  C++ Standard: ${CMAKE_CXX_STANDARD}")
message(STATUS "")
message(STATUS "Targets:")
message(STATUS "  - crate_reader")
message(STATUS "  - crate_writer")
message(STATUS "  - crate_internal_api")
message(STATUS "========================================")
