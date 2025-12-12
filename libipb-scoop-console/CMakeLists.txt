# Console Scoop Library
cmake_minimum_required(VERSION 3.16)

project(libipb-scoop-console VERSION 1.0.0)

# C++ Standard
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Find dependencies
find_package(jsoncpp REQUIRED)

# Include directories
include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/libipb-common/include
)

# Source files
set(CONSOLE_SCOOP_SOURCES
    src/console_scoop.cpp
)

# Create shared library
add_library(ipb-scoop-console SHARED ${CONSOLE_SCOOP_SOURCES})

# Link libraries
target_link_libraries(ipb-scoop-console
    ipb-common
    jsoncpp_lib
    pthread
)

# Compiler flags
target_compile_options(ipb-scoop-console PRIVATE
    -Wall -Wextra -Wpedantic
    -O3 -march=native
    -fPIC
)

# Install targets
install(TARGETS ipb-scoop-console
    LIBRARY DESTINATION lib
    ARCHIVE DESTINATION lib
)

install(DIRECTORY include/
    DESTINATION include
    FILES_MATCHING PATTERN "*.hpp"
)

# Export configuration
include(CMakePackageConfigHelpers)

configure_package_config_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/libipb-scoop-console-config.cmake.in"
    "${CMAKE_CURRENT_BINARY_DIR}/libipb-scoop-console-config.cmake"
    INSTALL_DESTINATION lib/cmake/libipb-scoop-console
)

write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/libipb-scoop-console-config-version.cmake"
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion
)

install(FILES
    "${CMAKE_CURRENT_BINARY_DIR}/libipb-scoop-console-config.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/libipb-scoop-console-config-version.cmake"
    DESTINATION lib/cmake/libipb-scoop-console
)

# Tests
if(BUILD_TESTING)
    add_subdirectory(tests)
endif()

# Examples
if(BUILD_EXAMPLES)
    add_subdirectory(examples)
endif()
