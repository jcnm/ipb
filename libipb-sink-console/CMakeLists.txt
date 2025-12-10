# Console Sink Library
cmake_minimum_required(VERSION 3.20)

project(libipb-sink-console
    VERSION 1.0.0
    DESCRIPTION "IPB Console Sink - High-performance console output for debugging and monitoring"
    LANGUAGES CXX
)

# Find required dependencies
find_package(PkgConfig REQUIRED)
pkg_check_modules(JSONCPP jsoncpp REQUIRED)

# Create the console sink library
add_library(ipb-sink-console SHARED
    src/console_sink.cpp
)

# Set target properties
set_target_properties(ipb-sink-console PROPERTIES
    VERSION ${PROJECT_VERSION}
    SOVERSION 1
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS OFF
)

# Include directories
target_include_directories(ipb-sink-console PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

# Link libraries
target_link_libraries(ipb-sink-console PUBLIC
    ipb-common
    ${JSONCPP_LIBRARIES}
    Threads::Threads
)

# Compiler flags
target_compile_options(ipb-sink-console PRIVATE
    ${JSONCPP_CFLAGS_OTHER}
)

# Real-time optimizations
if(ENABLE_REALTIME_OPTIMIZATIONS)
    target_compile_definitions(ipb-sink-console PRIVATE IPB_REALTIME_ENABLED)
endif()

# Performance optimizations
if(CMAKE_BUILD_TYPE STREQUAL "Release")
    target_compile_options(ipb-sink-console PRIVATE
        -O3
        -ffast-math
        -funroll-loops
        -finline-functions
    )
endif()

# Install targets
install(TARGETS ipb-sink-console
    EXPORT ipb-sink-console-targets
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

# Install headers
install(DIRECTORY include/
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
    FILES_MATCHING PATTERN "*.hpp"
)

# Install CMake config files
install(EXPORT ipb-sink-console-targets
    FILE ipb-sink-console-targets.cmake
    NAMESPACE ipb::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/ipb-sink-console
)

# Create config file
include(CMakePackageConfigHelpers)

configure_package_config_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/ipb-sink-console-config.cmake.in"
    "${CMAKE_CURRENT_BINARY_DIR}/ipb-sink-console-config.cmake"
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/ipb-sink-console
)

write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/ipb-sink-console-config-version.cmake"
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion
)

install(FILES
    "${CMAKE_CURRENT_BINARY_DIR}/ipb-sink-console-config.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/ipb-sink-console-config-version.cmake"
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/ipb-sink-console
)

# Tests
if(ENABLE_TESTS)
    add_subdirectory(tests)
endif()

# Examples
if(ENABLE_EXAMPLES)
    add_subdirectory(examples)
endif()

