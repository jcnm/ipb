# Syslog Sink Library
cmake_minimum_required(VERSION 3.16)

project(libipb-sink-syslog VERSION 1.2.0)

# C++ Standard
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Find dependencies
find_package(PkgConfig REQUIRED)
# Find jsoncpp (avoid conflicts)
if(NOT TARGET jsoncpp_lib AND NOT TARGET JsonCpp::JsonCpp)
    find_package(jsoncpp QUIET)
    if(NOT jsoncpp_FOUND)
        pkg_check_modules(JSONCPP REQUIRED jsoncpp)
    endif()
endif()

# Include directories
include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/libipb-common/include
)

# Source files
set(SYSLOG_SINK_SOURCES
    src/syslog_sink.cpp
)

# Create shared library
add_library(ipb-sink-syslog SHARED ${SYSLOG_SINK_SOURCES})

# Link libraries
target_link_libraries(ipb-sink-syslog
    ipb-common
    jsoncpp_lib
    pthread
)

# Compiler flags
target_compile_options(ipb-sink-syslog PRIVATE
    -Wall -Wextra -Wpedantic
    -O3 -march=native
    -fPIC
)

# Install targets
install(TARGETS ipb-sink-syslog
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
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/libipb-sink-syslog-config.cmake.in"
    "${CMAKE_CURRENT_BINARY_DIR}/libipb-sink-syslog-config.cmake"
    INSTALL_DESTINATION lib/cmake/libipb-sink-syslog
)

write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/libipb-sink-syslog-config-version.cmake"
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion
)

install(FILES
    "${CMAKE_CURRENT_BINARY_DIR}/libipb-sink-syslog-config.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/libipb-sink-syslog-config-version.cmake"
    DESTINATION lib/cmake/libipb-sink-syslog
)

# Tests
if(BUILD_TESTING)
    add_subdirectory(tests)
endif()

# Examples
if(BUILD_EXAMPLES)
    add_subdirectory(examples)
endif()

