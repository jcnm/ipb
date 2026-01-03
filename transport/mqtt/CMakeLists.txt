# MQTT Transport Library (Shared)
# This library provides the base MQTT client functionality shared by:
# - libipb-sink-mqtt
# - libipb-scoop-mqtt
# - libipb-sink-sparkplug
# - libipb-scoop-sparkplug
#
# Supports multiple backends:
# - Paho MQTT (default, general purpose)
# - coreMQTT (embedded, zero-allocation)

cmake_minimum_required(VERSION 3.16)

project(libipb-transport-mqtt VERSION 1.1.0)

# C++ Standard
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

#=============================================================================
# Backend Options
#=============================================================================

option(IPB_MQTT_BACKEND_PAHO "Enable Paho MQTT backend (default)" ON)
option(IPB_MQTT_BACKEND_COREMQTT "Enable coreMQTT backend (embedded)" OFF)
option(IPB_MQTT_DEFAULT_COREMQTT "Use coreMQTT as default backend" OFF)

#=============================================================================
# Find Dependencies
#=============================================================================

set(BACKENDS_AVAILABLE "")
set(BACKEND_SOURCES "")
set(BACKEND_LIBS "")
set(BACKEND_INCLUDES "")
set(BACKEND_DEFINES "")

# Paho MQTT Backend
if(IPB_MQTT_BACKEND_PAHO)
    # First try to use the CMake config provided by vcpkg or system installation
    # This is the preferred method as it properly handles import/export definitions on Windows
    find_package(PahoMqttCpp QUIET CONFIG)

    if(PahoMqttCpp_FOUND)
        message(STATUS "Paho MQTT backend: ENABLED (via CMake config)")
        list(APPEND BACKENDS_AVAILABLE "paho")
        list(APPEND BACKEND_SOURCES src/backends/paho_backend.cpp)
        # Use the imported target which handles all includes and definitions
        list(APPEND BACKEND_LIBS PahoMqttCpp::paho-mqttpp3)
        list(APPEND BACKEND_DEFINES IPB_HAS_PAHO=1)
        set(PAHO_FOUND_VIA_CONFIG TRUE)
    else()
        # Fallback to manual library finding for systems without CMake config
        find_library(PAHO_MQTT_C_LIB paho-mqtt3as)
        find_library(PAHO_MQTT_CPP_LIB paho-mqttpp3)
        find_path(PAHO_MQTT_CPP_INCLUDE mqtt/async_client.h)

        if(PAHO_MQTT_CPP_LIB AND PAHO_MQTT_C_LIB)
            message(STATUS "Paho MQTT backend: ENABLED (via find_library)")
            list(APPEND BACKENDS_AVAILABLE "paho")
            list(APPEND BACKEND_SOURCES src/backends/paho_backend.cpp)
            list(APPEND BACKEND_LIBS ${PAHO_MQTT_CPP_LIB} ${PAHO_MQTT_C_LIB})
            list(APPEND BACKEND_INCLUDES ${PAHO_MQTT_CPP_INCLUDE})
            list(APPEND BACKEND_DEFINES IPB_HAS_PAHO=1)
        else()
            message(STATUS "Paho MQTT backend: NOT FOUND")
            message(STATUS "  Install with: brew install paho-mqtt-c paho-mqtt-cpp (macOS)")
            message(STATUS "  Install with: sudo apt install libpaho-mqtt-dev libpaho-mqttpp-dev (Ubuntu)")
        endif()
    endif()
endif()

# coreMQTT Backend
if(IPB_MQTT_BACKEND_COREMQTT)
    find_path(COREMQTT_INCLUDE core_mqtt.h
        PATHS
            /usr/local/include
            /usr/include
            ${CMAKE_SOURCE_DIR}/external/coreMQTT/source/include
    )
    find_library(COREMQTT_LIB coremqtt
        PATHS
            /usr/local/lib
            /usr/lib
            ${CMAKE_SOURCE_DIR}/external/coreMQTT/build
    )

    if(COREMQTT_INCLUDE)
        message(STATUS "coreMQTT backend: ENABLED")
        list(APPEND BACKENDS_AVAILABLE "coremqtt")
        list(APPEND BACKEND_SOURCES src/backends/coremqtt_backend.cpp)
        list(APPEND BACKEND_INCLUDES ${COREMQTT_INCLUDE})
        list(APPEND BACKEND_DEFINES IPB_HAS_COREMQTT=1)
        if(COREMQTT_LIB)
            list(APPEND BACKEND_LIBS ${COREMQTT_LIB})
        endif()
    else()
        message(STATUS "coreMQTT backend: NOT FOUND (stub will be used)")
        # Still compile the stub version
        list(APPEND BACKEND_SOURCES src/backends/coremqtt_backend.cpp)
    endif()
else()
    # Compile stub even if not enabled (for API completeness)
    list(APPEND BACKEND_SOURCES src/backends/coremqtt_backend.cpp)
endif()

# Default backend selection
if(IPB_MQTT_DEFAULT_COREMQTT AND "coremqtt" IN_LIST BACKENDS_AVAILABLE)
    list(APPEND BACKEND_DEFINES IPB_DEFAULT_MQTT_BACKEND_COREMQTT=1)
    message(STATUS "Default MQTT backend: coreMQTT")
elseif("paho" IN_LIST BACKENDS_AVAILABLE)
    message(STATUS "Default MQTT backend: Paho")
else()
    message(WARNING "No MQTT backend available! Transport layer will not function.")
endif()

# Check we have at least one backend
if(NOT BACKENDS_AVAILABLE)
    message(WARNING "No MQTT backends found. Please install Paho MQTT or coreMQTT.")
endif()

#=============================================================================
# Source Files
#=============================================================================

set(TRANSPORT_MQTT_SOURCES
    src/mqtt_connection.cpp
    src/backends/backend_factory.cpp
    ${BACKEND_SOURCES}
)

#=============================================================================
# Library Target
#=============================================================================

add_library(ipb-transport-mqtt SHARED ${TRANSPORT_MQTT_SOURCES})

# Include directories
target_include_directories(ipb-transport-mqtt
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
    PRIVATE
        ${BACKEND_INCLUDES}
)

# Link dependencies
target_link_libraries(ipb-transport-mqtt
    PRIVATE
        ${BACKEND_LIBS}
)
if(NOT WIN32)
    target_link_libraries(ipb-transport-mqtt PUBLIC pthread)
endif()

# Find and link SSL if available (needed for TLS support)
find_package(OpenSSL QUIET)
if(OpenSSL_FOUND)
    target_link_libraries(ipb-transport-mqtt PRIVATE OpenSSL::SSL OpenSSL::Crypto)
else()
    find_library(SSL_LIB ssl)
    find_library(CRYPTO_LIB crypto)
    if(SSL_LIB AND CRYPTO_LIB)
        target_link_libraries(ipb-transport-mqtt PRIVATE ${SSL_LIB} ${CRYPTO_LIB})
    endif()
endif()

# Link libipb-security if available (for TLS support in coreMQTT backend)
option(IPB_MQTT_USE_SECURITY "Use libipb-security for TLS in coreMQTT backend" ON)
if(IPB_MQTT_USE_SECURITY AND TARGET ipb-security)
    target_link_libraries(ipb-transport-mqtt PRIVATE ipb-security)
    target_include_directories(ipb-transport-mqtt PRIVATE
        ${CMAKE_SOURCE_DIR}/core/security/include
    )
    target_compile_definitions(ipb-transport-mqtt PRIVATE IPB_HAS_SECURITY=1)
    message(STATUS "MQTT transport: libipb-security integration ENABLED")
endif()

# Compile definitions
target_compile_definitions(ipb-transport-mqtt
    PRIVATE
        ${BACKEND_DEFINES}
)

# Compiler options
if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU" OR CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
    target_compile_options(ipb-transport-mqtt PRIVATE
        -Wall -Wextra -Wpedantic
        -fPIC
    )
    if(CMAKE_BUILD_TYPE STREQUAL "Release")
        target_compile_options(ipb-transport-mqtt PRIVATE -O3)
        if(NOT APPLE)
            target_compile_options(ipb-transport-mqtt PRIVATE -march=native)
        endif()
    endif()
elseif(MSVC)
    target_compile_options(ipb-transport-mqtt PRIVATE /W4)
    if(CMAKE_BUILD_TYPE STREQUAL "Release")
        target_compile_options(ipb-transport-mqtt PRIVATE /O2)
    endif()
endif()

#=============================================================================
# Library Properties
#=============================================================================

set_target_properties(ipb-transport-mqtt PROPERTIES
    VERSION ${PROJECT_VERSION}
    SOVERSION 1
    OUTPUT_NAME ipb-transport-mqtt
)

#=============================================================================
# Installation
#=============================================================================

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

#=============================================================================
# Alias for Parent Project
#=============================================================================

add_library(ipb::transport-mqtt ALIAS ipb-transport-mqtt)

#=============================================================================
# Summary
#=============================================================================

message(STATUS "")
message(STATUS "MQTT Transport Configuration:")
message(STATUS "  Available backends: ${BACKENDS_AVAILABLE}")
message(STATUS "  Backend sources: ${BACKEND_SOURCES}")
message(STATUS "")
