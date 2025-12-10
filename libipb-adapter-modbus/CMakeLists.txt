# Modbus Adapter Library
cmake_minimum_required(VERSION 3.16)

project(libipb-adapter-modbus VERSION 1.2.0)

# C++ Standard
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Find dependencies
find_package(PkgConfig REQUIRED)
find_package(jsoncpp REQUIRED)

# Find libmodbus
pkg_check_modules(LIBMODBUS REQUIRED libmodbus)

# Include directories
include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/libipb-common/include
    ${LIBMODBUS_INCLUDE_DIRS}
)

# Source files
set(MODBUS_ADAPTER_SOURCES
    src/modbus_adapter.cpp
)

# Create shared library
add_library(ipb-adapter-modbus SHARED ${MODBUS_ADAPTER_SOURCES})

# Link libraries
target_link_libraries(ipb-adapter-modbus
    ipb-common
    jsoncpp_lib
    ${LIBMODBUS_LIBRARIES}
    pthread
)

# Compiler flags
target_compile_options(ipb-adapter-modbus PRIVATE
    -Wall -Wextra -Wpedantic
    -O3 -march=native
    -fPIC
    ${LIBMODBUS_CFLAGS_OTHER}
)

# Link directories
target_link_directories(ipb-adapter-modbus PRIVATE
    ${LIBMODBUS_LIBRARY_DIRS}
)

# Install targets
install(TARGETS ipb-adapter-modbus
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
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/libipb-adapter-modbus-config.cmake.in"
    "${CMAKE_CURRENT_BINARY_DIR}/libipb-adapter-modbus-config.cmake"
    INSTALL_DESTINATION lib/cmake/libipb-adapter-modbus
)

write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/libipb-adapter-modbus-config-version.cmake"
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion
)

install(FILES
    "${CMAKE_CURRENT_BINARY_DIR}/libipb-adapter-modbus-config.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/libipb-adapter-modbus-config-version.cmake"
    DESTINATION lib/cmake/libipb-adapter-modbus
)

# Tests
if(BUILD_TESTING)
    add_subdirectory(tests)
endif()

# Examples
if(BUILD_EXAMPLES)
    add_subdirectory(examples)
endif()

