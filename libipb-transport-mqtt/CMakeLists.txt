# MQTT Transport Library (Shared)
# This library provides the base MQTT client functionality shared by:
# - libipb-sink-mqtt
# - libipb-scoop-mqtt
# - libipb-sink-sparkplug
# - libipb-scoop-sparkplug
#
# This avoids duplicating the Paho MQTT library across multiple components.

cmake_minimum_required(VERSION 3.16)

project(libipb-transport-mqtt VERSION 1.0.0)

# C++ Standard
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Find Paho MQTT C++ library
find_library(PAHO_MQTT_C_LIB paho-mqtt3as REQUIRED)
find_library(PAHO_MQTT_CPP_LIB paho-mqttpp3 REQUIRED)
find_path(PAHO_MQTT_CPP_INCLUDE mqtt/async_client.h)

if(NOT PAHO_MQTT_CPP_LIB OR NOT PAHO_MQTT_C_LIB)
    message(FATAL_ERROR "Paho MQTT libraries not found. Please install:
        Ubuntu/Debian: sudo apt install libpaho-mqtt-dev libpaho-mqttpp-dev
        macOS: brew install paho-mqtt-c paho-mqtt-cpp")
endif()

# Source files
set(TRANSPORT_MQTT_SOURCES
    src/mqtt_connection.cpp
)

# Create shared library
add_library(ipb-transport-mqtt SHARED ${TRANSPORT_MQTT_SOURCES})

# Include directories
target_include_directories(ipb-transport-mqtt
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
    PRIVATE
        ${PAHO_MQTT_CPP_INCLUDE}
)

# Link dependencies
target_link_libraries(ipb-transport-mqtt
    PUBLIC
        ${PAHO_MQTT_CPP_LIB}
        ${PAHO_MQTT_C_LIB}
        pthread
        ssl
        crypto
)

# Compiler options
target_compile_options(ipb-transport-mqtt PRIVATE
    -Wall -Wextra -Wpedantic
    -fPIC
)

if(CMAKE_BUILD_TYPE STREQUAL "Release")
    target_compile_options(ipb-transport-mqtt PRIVATE -O3 -march=native)
endif()

# Set library properties
set_target_properties(ipb-transport-mqtt PROPERTIES
    VERSION ${PROJECT_VERSION}
    SOVERSION 1
    OUTPUT_NAME ipb-transport-mqtt
)

# Install targets
install(TARGETS ipb-transport-mqtt
    EXPORT ipb-transport-mqtt-targets
    LIBRARY DESTINATION lib
    ARCHIVE DESTINATION lib
    RUNTIME DESTINATION bin
)

install(DIRECTORY include/
    DESTINATION include
    FILES_MATCHING PATTERN "*.hpp"
)

# Export targets
install(EXPORT ipb-transport-mqtt-targets
    FILE ipb-transport-mqtt-targets.cmake
    NAMESPACE ipb::
    DESTINATION lib/cmake/ipb-transport-mqtt
)

# Package configuration
include(CMakePackageConfigHelpers)

configure_package_config_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/ipb-transport-mqtt-config.cmake.in"
    "${CMAKE_CURRENT_BINARY_DIR}/ipb-transport-mqtt-config.cmake"
    INSTALL_DESTINATION lib/cmake/ipb-transport-mqtt
)

write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/ipb-transport-mqtt-config-version.cmake"
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion
)

install(FILES
    "${CMAKE_CURRENT_BINARY_DIR}/ipb-transport-mqtt-config.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/ipb-transport-mqtt-config-version.cmake"
    DESTINATION lib/cmake/ipb-transport-mqtt
)

# Alias for use in parent project
add_library(ipb::transport-mqtt ALIAS ipb-transport-mqtt)
