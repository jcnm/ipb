# IPB Router Library
cmake_minimum_required(VERSION 3.16)

project(libipb-router VERSION 1.0.0 LANGUAGES CXX)

# C++ Standard
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Find dependencies
find_package(Threads REQUIRED)

# Library sources
set(ROUTER_SOURCES
    src/router.cpp
)

set(ROUTER_HEADERS
    include/ipb/router/router.hpp
)

# Create library
add_library(ipb-router ${ROUTER_SOURCES})

# Set target properties
set_target_properties(ipb-router PROPERTIES
    VERSION ${PROJECT_VERSION}
    SOVERSION ${PROJECT_VERSION_MAJOR}
    PUBLIC_HEADER "${ROUTER_HEADERS}"
)

# Include directories
target_include_directories(ipb-router
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/core/components/include>
        $<INSTALL_INTERFACE:include>
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

# Link libraries
target_link_libraries(ipb-router
    PUBLIC
        ipb-common
        Threads::Threads
)


# Compiler flags (platform-specific)
if(MSVC)
    # Compiler flags - platform specific
    target_compile_options(ipb-router PRIVATE
        $<$<CXX_COMPILER_ID:MSVC>:/W4 /permissive->
        $<$<CXX_COMPILER_ID:MSVC>:$<$<CONFIG:Release>:/O2>>
        $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-Wall -Wextra -Wpedantic>
        $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:$<$<CONFIG:Release>:-O3>>
        $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:$<$<CONFIG:Debug>:-g -O0>>
    )
else()
    target_compile_options(ipb-router PRIVATE
        -Wall -Wextra -Wpedantic
        $<$<CONFIG:Release>:-O3>
        $<$<CONFIG:Debug>:-g -O0>
    )
endif()

# Install targets
install(TARGETS ipb-router
    EXPORT ipb-router-targets
    LIBRARY DESTINATION lib
    ARCHIVE DESTINATION lib
    RUNTIME DESTINATION bin
    PUBLIC_HEADER DESTINATION include/ipb/router
)

# Install export
install(EXPORT ipb-router-targets
    FILE ipb-router-targets.cmake
    NAMESPACE ipb::
    DESTINATION lib/cmake/ipb-router
)

# Create config files
include(CMakePackageConfigHelpers)

configure_package_config_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/ipb-router-config.cmake.in"
    "${CMAKE_CURRENT_BINARY_DIR}/ipb-router-config.cmake"
    INSTALL_DESTINATION lib/cmake/ipb-router
)

write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/ipb-router-config-version.cmake"
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion
)

install(FILES
    "${CMAKE_CURRENT_BINARY_DIR}/ipb-router-config.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/ipb-router-config-version.cmake"
    DESTINATION lib/cmake/ipb-router
)

# pkg-config file
configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/ipb-router.pc.in"
    "${CMAKE_CURRENT_BINARY_DIR}/ipb-router.pc"
    @ONLY
)

install(FILES "${CMAKE_CURRENT_BINARY_DIR}/ipb-router.pc"
    DESTINATION lib/pkgconfig
)

# Examples
option(BUILD_ROUTER_EXAMPLES "Build router examples" OFF)
if(BUILD_ROUTER_EXAMPLES AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/examples")
    add_subdirectory(examples)
endif()

# Tests
option(BUILD_ROUTER_TESTS "Build router tests" OFF)
if(BUILD_ROUTER_TESTS AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tests")
    add_subdirectory(tests)
endif()

# Configuration summary
message(STATUS "")
message(STATUS "IPB Router Library Configuration:")
message(STATUS "  Version: ${PROJECT_VERSION}")
message(STATUS "  Build type: ${CMAKE_BUILD_TYPE}")
message(STATUS "  Install prefix: ${CMAKE_INSTALL_PREFIX}")
message(STATUS "")

