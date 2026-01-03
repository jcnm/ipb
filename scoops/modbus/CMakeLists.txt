# Modbus Scoop Library
cmake_minimum_required(VERSION 3.16)

project(libipb-scoop-modbus VERSION 1.2.0)

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

# Find jsoncpp (optional - may not be present in EMBEDDED mode)
find_package(jsoncpp QUIET)
if(NOT jsoncpp_FOUND AND NOT TARGET jsoncpp_lib)
    if(PkgConfig_FOUND)
        pkg_check_modules(JSONCPP jsoncpp)
    endif()
    if(NOT JSONCPP_FOUND AND NOT TARGET jsoncpp_lib)
        message(WARNING "jsoncpp not found - Modbus scoop will be disabled")
        return()
    endif()
endif()

# Find libmodbus (not available on Windows)
if(NOT PkgConfig_FOUND)
    message(STATUS "pkg-config not available - Modbus scoop disabled on this platform")
    return()
endif()
pkg_check_modules(LIBMODBUS libmodbus)
if(NOT LIBMODBUS_FOUND)
    message(WARNING "libmodbus not found - Modbus scoop will be disabled")
    message(STATUS "  Install with: sudo apt install libmodbus-dev (Ubuntu)")
    message(STATUS "  Install with: brew install libmodbus (macOS)")
    return()
endif()

# Include directories
include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/core/common/include
    ${LIBMODBUS_INCLUDE_DIRS}
)

# Source files
set(MODBUS_SCOOP_SOURCES
    src/modbus_scoop.cpp
)

# Create shared library
add_library(ipb-scoop-modbus SHARED ${MODBUS_SCOOP_SOURCES})

# Link libraries
target_link_libraries(ipb-scoop-modbus
    ipb-common
    jsoncpp_lib
    ${LIBMODBUS_LIBRARIES}
)
if(NOT WIN32)
    target_link_libraries(ipb-scoop-modbus pthread)
endif()

# Compiler flags
if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU" OR CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
    target_compile_options(ipb-scoop-modbus PRIVATE
        -Wall -Wextra -Wpedantic
        -O3
        -fPIC
        ${LIBMODBUS_CFLAGS_OTHER}
    )
    if(NOT APPLE)
        target_compile_options(ipb-scoop-modbus PRIVATE -march=native)
    endif()
elseif(MSVC)
    target_compile_options(ipb-scoop-modbus PRIVATE /W4 /O2)
endif()

# Link directories
target_link_directories(ipb-scoop-modbus PRIVATE
    ${LIBMODBUS_LIBRARY_DIRS}
)

# Install targets
install(TARGETS ipb-scoop-modbus
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
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/libipb-scoop-modbus-config.cmake.in"
    "${CMAKE_CURRENT_BINARY_DIR}/libipb-scoop-modbus-config.cmake"
    INSTALL_DESTINATION lib/cmake/libipb-scoop-modbus
)

write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/libipb-scoop-modbus-config-version.cmake"
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion
)

install(FILES
    "${CMAKE_CURRENT_BINARY_DIR}/libipb-scoop-modbus-config.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/libipb-scoop-modbus-config-version.cmake"
    DESTINATION lib/cmake/libipb-scoop-modbus
)

# Tests
if(BUILD_TESTING AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tests")
    add_subdirectory(tests)
endif()

# Examples
if(BUILD_EXAMPLES AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/examples")
    add_subdirectory(examples)
endif()
