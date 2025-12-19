# Syslog Sink Library
cmake_minimum_required(VERSION 3.16)

project(libipb-sink-syslog VERSION 1.2.0)

# C++ Standard
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Find dependencies
# PkgConfig is optional on Windows
if(NOT WIN32)
    find_package(PkgConfig REQUIRED)
else()
    find_package(PkgConfig QUIET)
endif()
# Find jsoncpp (avoid conflicts)
set(SYSLOG_JSONCPP_TARGET "")
if(TARGET jsoncpp_lib)
    set(SYSLOG_JSONCPP_TARGET jsoncpp_lib)
elseif(TARGET JsonCpp::JsonCpp)
    set(SYSLOG_JSONCPP_TARGET JsonCpp::JsonCpp)
else()
    find_package(jsoncpp QUIET)
    if(jsoncpp_FOUND)
        if(TARGET jsoncpp_lib)
            set(SYSLOG_JSONCPP_TARGET jsoncpp_lib)
        elseif(TARGET JsonCpp::JsonCpp)
            set(SYSLOG_JSONCPP_TARGET JsonCpp::JsonCpp)
        endif()
    endif()
    if(NOT SYSLOG_JSONCPP_TARGET AND PkgConfig_FOUND)
        pkg_check_modules(JSONCPP REQUIRED jsoncpp)
    endif()
endif()

# Include directories
include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/core/common/include
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
    pthread
)

# Link jsoncpp based on how it was found
if(SYSLOG_JSONCPP_TARGET)
    target_link_libraries(ipb-sink-syslog ${SYSLOG_JSONCPP_TARGET})
elseif(JSONCPP_FOUND)
    target_include_directories(ipb-sink-syslog PRIVATE ${JSONCPP_INCLUDE_DIRS})
    target_link_directories(ipb-sink-syslog PRIVATE ${JSONCPP_LIBRARY_DIRS})
    target_link_libraries(ipb-sink-syslog ${JSONCPP_LIBRARIES})
endif()

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
if(BUILD_TESTING AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tests/CMakeLists.txt")
    add_subdirectory(tests)
endif()

# Examples
if(BUILD_EXAMPLES)
    add_subdirectory(examples)
endif()

