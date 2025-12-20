# IPB Bridge - Lightweight Edge/Embedded Application
cmake_minimum_required(VERSION 3.16)

project(ipb-bridge
    VERSION 1.0.0
    DESCRIPTION "IPB Bridge - Lightweight Industrial Protocol Bridge for Edge/Embedded"
    LANGUAGES CXX
)

# C++ Standard
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Build options
option(IPB_BRIDGE_STATIC "Build statically linked binary" OFF)
option(IPB_BRIDGE_MINIMAL "Minimal build with reduced features" OFF)
option(IPB_BRIDGE_ENABLE_WATCHDOG "Enable hardware watchdog support" ON)

# Find required dependencies
find_package(Threads REQUIRED)

# Optional: yaml-cpp (for config loading, can be disabled for minimal build)
if(NOT IPB_BRIDGE_MINIMAL)
    # Check for yaml-cpp target (may be yaml-cpp or yaml-cpp::yaml-cpp)
    if(TARGET yaml-cpp)
        set(HAS_YAML_CPP TRUE)
        set(YAML_CPP_TARGET yaml-cpp)
    elseif(TARGET yaml-cpp::yaml-cpp)
        set(HAS_YAML_CPP TRUE)
        set(YAML_CPP_TARGET yaml-cpp::yaml-cpp)
    else()
        find_package(yaml-cpp QUIET)
        if(yaml-cpp_FOUND)
            set(HAS_YAML_CPP TRUE)
            if(TARGET yaml-cpp::yaml-cpp)
                set(YAML_CPP_TARGET yaml-cpp::yaml-cpp)
            else()
                set(YAML_CPP_TARGET yaml-cpp)
            endif()
        endif()
    endif()

    # On macOS, yaml-cpp CMake config may not have proper include directories
    # Try to find and add them explicitly if needed
    if(HAS_YAML_CPP AND APPLE AND TARGET ${YAML_CPP_TARGET})
        # Check if we need to find include directories
        get_target_property(_yaml_inc ${YAML_CPP_TARGET} INTERFACE_INCLUDE_DIRECTORIES)
        if(NOT _yaml_inc OR "${_yaml_inc}" STREQUAL "_yaml_inc-NOTFOUND")
            # Try Homebrew paths
            find_path(YAML_CPP_INCLUDE_DIR yaml-cpp/yaml.h
                PATHS
                    /opt/homebrew/include
                    /usr/local/include
                    /usr/include
                NO_DEFAULT_PATH
            )
            if(YAML_CPP_INCLUDE_DIR)
                set(YAML_CPP_BRIDGE_INCLUDE_DIR "${YAML_CPP_INCLUDE_DIR}")
                message(STATUS "ipb-bridge: Found yaml-cpp includes at ${YAML_CPP_BRIDGE_INCLUDE_DIR}")
            endif()
        endif()
    endif()
endif()

# Source files
set(BRIDGE_SOURCES
    src/main.cpp
    src/bridge.cpp
    src/config.cpp
)

# Create executable
add_executable(ipb-bridge ${BRIDGE_SOURCES})

# Include directories
target_include_directories(ipb-bridge PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/core/common/include
    ${CMAKE_SOURCE_DIR}/core/router/include
    ${CMAKE_SOURCE_DIR}/core/components/include
)

# Link libraries (base)
target_link_libraries(ipb-bridge PRIVATE
    ipb-common
    Threads::Threads
)

# Conditionally link ipb-router (optional for minimal builds)
if(NOT IPB_BRIDGE_MINIMAL AND TARGET ipb-router)
    target_link_libraries(ipb-bridge PRIVATE ipb-router)
    target_compile_definitions(ipb-bridge PRIVATE IPB_BRIDGE_HAS_ROUTER=1)
endif()

# Conditionally link yaml-cpp
if(HAS_YAML_CPP)
    target_link_libraries(ipb-bridge PRIVATE ${YAML_CPP_TARGET})
    target_compile_definitions(ipb-bridge PRIVATE IPB_BRIDGE_HAS_YAML=1)
    # Add explicit include directory on macOS if needed
    if(YAML_CPP_BRIDGE_INCLUDE_DIR)
        target_include_directories(ipb-bridge PRIVATE ${YAML_CPP_BRIDGE_INCLUDE_DIR})
    endif()
endif()

# Conditionally link sinks (based on build configuration)
if(ENABLE_CONSOLE_SINK AND TARGET ipb-sink-console)
    target_link_libraries(ipb-bridge PRIVATE ipb-sink-console)
    target_compile_definitions(ipb-bridge PRIVATE IPB_HAS_CONSOLE_SINK=1)
endif()

if(ENABLE_SYSLOG_SINK AND TARGET ipb-sink-syslog)
    target_link_libraries(ipb-bridge PRIVATE ipb-sink-syslog)
    target_compile_definitions(ipb-bridge PRIVATE IPB_HAS_SYSLOG_SINK=1)
endif()

if(ENABLE_MQTT_SINK AND TARGET ipb-sink-mqtt)
    target_link_libraries(ipb-bridge PRIVATE ipb-sink-mqtt)
    target_compile_definitions(ipb-bridge PRIVATE IPB_HAS_MQTT_SINK=1)
endif()

# Conditionally link scoops
if(ENABLE_MODBUS_SCOOP AND TARGET ipb-scoop-modbus)
    target_link_libraries(ipb-bridge PRIVATE ipb-scoop-modbus)
    target_compile_definitions(ipb-bridge PRIVATE IPB_HAS_MODBUS_SCOOP=1)
endif()

# Compile definitions
target_compile_definitions(ipb-bridge PRIVATE
    IPB_BRIDGE_VERSION="${PROJECT_VERSION}"
)

if(IPB_BRIDGE_MINIMAL)
    target_compile_definitions(ipb-bridge PRIVATE IPB_BRIDGE_MINIMAL=1)
endif()

if(IPB_BRIDGE_ENABLE_WATCHDOG)
    target_compile_definitions(ipb-bridge PRIVATE IPB_BRIDGE_WATCHDOG=1)
endif()

# Compiler options - optimized for embedded
target_compile_features(ipb-bridge PRIVATE cxx_std_20)

target_compile_options(ipb-bridge PRIVATE
    -Wall -Wextra -Wpedantic
    -fno-rtti            # Reduce binary size
    -ffunction-sections  # Enable dead code elimination
    -fdata-sections
)

# Link options for size optimization
target_link_options(ipb-bridge PRIVATE
    -Wl,--gc-sections    # Remove unused sections
)

# Static linking option
if(IPB_BRIDGE_STATIC)
    target_link_options(ipb-bridge PRIVATE -static)
endif()

# Release optimizations
if(CMAKE_BUILD_TYPE STREQUAL "Release")
    target_compile_options(ipb-bridge PRIVATE
        -Os              # Optimize for size (embedded-friendly)
        -flto            # Link-time optimization
    )
    target_link_options(ipb-bridge PRIVATE -flto)
endif()

# Debug options
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    target_compile_options(ipb-bridge PRIVATE -g3)
    target_compile_definitions(ipb-bridge PRIVATE IPB_DEBUG=1)
endif()

# Platform-specific settings
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    target_link_libraries(ipb-bridge PRIVATE pthread)

    # Real-time scheduling support
    if(IPB_BRIDGE_ENABLE_WATCHDOG)
        target_link_libraries(ipb-bridge PRIVATE rt)
    endif()
endif()

# Installation
install(TARGETS ipb-bridge
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

# Install example configuration
install(FILES
    config/ipb-bridge.yaml.example
    DESTINATION ${CMAKE_INSTALL_SYSCONFDIR}/ipb
    OPTIONAL
)

# Systemd service for Linux edge devices
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    install(FILES
        systemd/ipb-bridge.service
        DESTINATION ${CMAKE_INSTALL_PREFIX}/lib/systemd/system
        OPTIONAL
    )
endif()

# Summary
message(STATUS "")
message(STATUS "IPB Bridge Configuration:")
message(STATUS "  Version: ${PROJECT_VERSION}")
message(STATUS "  Static linking: ${IPB_BRIDGE_STATIC}")
message(STATUS "  Minimal build: ${IPB_BRIDGE_MINIMAL}")
message(STATUS "  Watchdog support: ${IPB_BRIDGE_ENABLE_WATCHDOG}")
message(STATUS "  YAML config: ${HAS_YAML_CPP}")
message(STATUS "")
