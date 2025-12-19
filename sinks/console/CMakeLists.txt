# Console Sink Library
cmake_minimum_required(VERSION 3.20)

project(libipb-sink-console
    VERSION 1.0.0
    DESCRIPTION "IPB Console Sink - High-performance console output for debugging and monitoring"
    LANGUAGES CXX
)

# Find required dependencies
# PkgConfig is optional on Windows
if(NOT WIN32)
    find_package(PkgConfig REQUIRED)
else()
    find_package(PkgConfig QUIET)
endif()

# jsoncpp is optional for EMBEDDED mode
# First check if jsoncpp target already exists (from parent CMakeLists.txt)
if(TARGET jsoncpp_lib OR TARGET JsonCpp::JsonCpp)
    set(CONSOLE_HAS_JSONCPP TRUE)
elseif(NOT IPB_BUILD_MODE STREQUAL "EMBEDDED")
    # Try CMake find_package first
    find_package(jsoncpp QUIET)
    if(jsoncpp_FOUND OR TARGET jsoncpp_lib OR TARGET JsonCpp::JsonCpp)
        set(CONSOLE_HAS_JSONCPP TRUE)
    elseif(PkgConfig_FOUND)
        pkg_check_modules(JSONCPP jsoncpp REQUIRED)
        set(CONSOLE_HAS_JSONCPP TRUE)
    else()
        message(FATAL_ERROR "jsoncpp not found and pkg-config not available")
    endif()
else()
    # EMBEDDED mode - jsoncpp is optional
    find_package(jsoncpp QUIET)
    if(jsoncpp_FOUND OR TARGET jsoncpp_lib OR TARGET JsonCpp::JsonCpp)
        set(CONSOLE_HAS_JSONCPP TRUE)
    elseif(PkgConfig_FOUND)
        pkg_check_modules(JSONCPP jsoncpp QUIET)
        if(JSONCPP_FOUND)
            set(CONSOLE_HAS_JSONCPP TRUE)
        else()
            set(CONSOLE_HAS_JSONCPP FALSE)
            message(STATUS "Console sink: jsoncpp not available in EMBEDDED mode, using fallback JSON")
        endif()
    else()
        set(CONSOLE_HAS_JSONCPP FALSE)
        message(STATUS "Console sink: jsoncpp not available in EMBEDDED mode, using fallback JSON")
    endif()
endif()

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
    Threads::Threads
)

# Conditionally add jsoncpp
if(CONSOLE_HAS_JSONCPP)
    # Link to the appropriate target or use pkg-config variables
    if(TARGET JsonCpp::JsonCpp)
        target_link_libraries(ipb-sink-console PRIVATE JsonCpp::JsonCpp)
    elseif(TARGET jsoncpp_lib)
        target_link_libraries(ipb-sink-console PRIVATE jsoncpp_lib)
    elseif(JSONCPP_FOUND)
        # Found via pkg-config
        target_include_directories(ipb-sink-console PRIVATE ${JSONCPP_INCLUDE_DIRS})
        target_link_directories(ipb-sink-console PRIVATE ${JSONCPP_LIBRARY_DIRS})
        target_link_libraries(ipb-sink-console PRIVATE ${JSONCPP_LIBRARIES})
        target_compile_options(ipb-sink-console PRIVATE ${JSONCPP_CFLAGS_OTHER})
    endif()
    target_compile_definitions(ipb-sink-console PRIVATE IPB_CONSOLE_HAS_JSONCPP)
endif()

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
if(ENABLE_TESTS AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tests")
    add_subdirectory(tests)
endif()

# Examples
if(ENABLE_EXAMPLES AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/examples")
    add_subdirectory(examples)
endif()

