# IPB - Industrial Protocol Bridge
cmake_minimum_required(VERSION 3.16)

project(IPB VERSION 1.4.0 LANGUAGES CXX)

# C++ Standard
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Build options
option(BUILD_TESTING "Build tests" OFF)
option(BUILD_EXAMPLES "Build examples" ON)
option(BUILD_ALL_COMPONENTS "Build all components" ON)

# Individual component options
option(BUILD_COMMON "Build libipb-common" ON)
option(BUILD_ROUTER "Build libipb-router" ON)
option(BUILD_GATE "Build ipb-gate" ON)

# Sink options
option(ENABLE_CONSOLE_SINK "Enable Console sink" ON)
option(ENABLE_SYSLOG_SINK "Enable Syslog sink" ON)
option(ENABLE_MQTT_SINK "Enable MQTT sink" ON)

# Scoop options (data collectors)
option(ENABLE_MODBUS_SCOOP "Enable Modbus scoop" OFF)
option(ENABLE_OPCUA_SCOOP "Enable OPC UA scoop" OFF)
option(ENABLE_SPARKPLUG_SCOOP "Enable Sparkplug B scoop" OFF)

# Sparkplug sink option
option(ENABLE_SPARKPLUG_SINK "Enable Sparkplug B sink" OFF)

# Transport options (shared libraries)
option(BUILD_TRANSPORT_MQTT "Build MQTT transport (shared by MQTT/Sparkplug)" ON)

# Performance options
option(ENABLE_OPTIMIZATIONS "Enable performance optimizations" ON)
option(ENABLE_LTO "Enable Link Time Optimization" OFF)

# Development options
option(ENABLE_SANITIZERS "Enable sanitizers (Debug builds)" OFF)
option(ENABLE_COVERAGE "Enable code coverage" OFF)

# Platform detection
if(APPLE)
    set(PLATFORM_MACOS TRUE)
    # Use Homebrew prefix for dependencies
    if(NOT CMAKE_PREFIX_PATH)
        execute_process(
            COMMAND brew --prefix
            OUTPUT_VARIABLE HOMEBREW_PREFIX
            OUTPUT_STRIP_TRAILING_WHITESPACE
            ERROR_QUIET
        )
        if(HOMEBREW_PREFIX)
            set(CMAKE_PREFIX_PATH ${HOMEBREW_PREFIX})
            message(STATUS "Using Homebrew prefix: ${HOMEBREW_PREFIX}")
        endif()
    endif()
elseif(UNIX)
    set(PLATFORM_LINUX TRUE)
endif()

# Find common dependencies
find_package(PkgConfig REQUIRED)
find_package(Threads REQUIRED)

# Find jsoncpp (handle multiple target names and conflicts)
if(NOT TARGET jsoncpp_lib AND NOT TARGET JsonCpp::JsonCpp)
    find_package(jsoncpp QUIET)
    if(NOT jsoncpp_FOUND)
        pkg_check_modules(JSONCPP REQUIRED jsoncpp)
        # Create imported target
        add_library(jsoncpp_lib INTERFACE IMPORTED)
        set_target_properties(jsoncpp_lib PROPERTIES
            INTERFACE_INCLUDE_DIRECTORIES "${JSONCPP_INCLUDE_DIRS}"
            INTERFACE_LINK_LIBRARIES "${JSONCPP_LIBRARIES}"
        )
    endif()
else()
    message(STATUS "jsoncpp target already exists, reusing it")
endif()

# Find YAML-CPP
find_package(yaml-cpp QUIET)
if(NOT yaml-cpp_FOUND)
    pkg_check_modules(YAML_CPP REQUIRED yaml-cpp)
endif()

# Global include directories
include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}/libipb-common/include
)

# Global compiler flags
if(ENABLE_OPTIMIZATIONS)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra -Wpedantic")
    set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -O3")
    
    if(NOT APPLE)
        set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -march=native")
    endif()
    
    set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -g -O0")
endif()

# Link Time Optimization
if(ENABLE_LTO)
    include(CheckIPOSupported)
    check_ipo_supported(RESULT LTO_SUPPORTED OUTPUT LTO_ERROR)
    if(LTO_SUPPORTED)
        set(CMAKE_INTERPROCEDURAL_OPTIMIZATION TRUE)
        message(STATUS "Link Time Optimization enabled")
    else()
        message(WARNING "LTO not supported: ${LTO_ERROR}")
    endif()
endif()

# Sanitizers (Debug builds only)
if(ENABLE_SANITIZERS AND CMAKE_BUILD_TYPE STREQUAL "Debug")
    set(SANITIZER_FLAGS "-fsanitize=address -fsanitize=undefined -fno-omit-frame-pointer")
    set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} ${SANITIZER_FLAGS}")
    set(CMAKE_LINKER_FLAGS_DEBUG "${CMAKE_LINKER_FLAGS_DEBUG} ${SANITIZER_FLAGS}")
    message(STATUS "Sanitizers enabled for Debug builds")
endif()

# Code coverage
if(ENABLE_COVERAGE AND CMAKE_BUILD_TYPE STREQUAL "Debug")
    set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} --coverage")
    set(CMAKE_LINKER_FLAGS_DEBUG "${CMAKE_LINKER_FLAGS_DEBUG} --coverage")
    message(STATUS "Code coverage enabled")
endif()

# Helper function to conditionally add subdirectory
function(conditional_add_subdirectory dir condition)
    if(${condition})
        if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/${dir}/CMakeLists.txt")
            add_subdirectory(${dir})
            message(STATUS "Building component: ${dir}")
        else()
            message(WARNING "Component ${dir} requested but CMakeLists.txt not found")
        endif()
    else()
        message(STATUS "Skipping component: ${dir}")
    endif()
endfunction()

# Build components based on options
if(BUILD_ALL_COMPONENTS OR BUILD_COMMON)
    conditional_add_subdirectory(libipb-common TRUE)
endif()

if(BUILD_ALL_COMPONENTS OR BUILD_ROUTER)
    conditional_add_subdirectory(libipb-router BUILD_ROUTER)
endif()

# Sinks
if(BUILD_ALL_COMPONENTS OR ENABLE_CONSOLE_SINK)
    conditional_add_subdirectory(libipb-sink-console ENABLE_CONSOLE_SINK)
endif()

if(BUILD_ALL_COMPONENTS OR ENABLE_SYSLOG_SINK)
    conditional_add_subdirectory(libipb-sink-syslog ENABLE_SYSLOG_SINK)
endif()

# MQTT Transport Layer (shared by MQTT sink/scoop and Sparkplug sink/scoop)
# Build if any MQTT-based component is enabled
set(NEED_MQTT_TRANSPORT FALSE)
if(ENABLE_MQTT_SINK OR ENABLE_SPARKPLUG_SINK OR ENABLE_SPARKPLUG_SCOOP OR BUILD_TRANSPORT_MQTT)
    set(NEED_MQTT_TRANSPORT TRUE)
endif()

if(NEED_MQTT_TRANSPORT)
    # Check for MQTT dependencies
    find_library(PAHO_MQTT_CPP_LIB paho-mqttpp3)
    find_library(PAHO_MQTT_C_LIB paho-mqtt3as)

    if(PAHO_MQTT_CPP_LIB AND PAHO_MQTT_C_LIB)
        # Build shared transport library first
        conditional_add_subdirectory(libipb-transport-mqtt TRUE)
        message(STATUS "MQTT transport library enabled")
        set(MQTT_TRANSPORT_AVAILABLE TRUE)
    else()
        message(WARNING "MQTT libraries not found. MQTT/Sparkplug components disabled.")
        message(STATUS "Install with: brew install paho-mqtt-c paho-mqtt-cpp (macOS)")
        message(STATUS "Install with: sudo apt install libpaho-mqtt-dev libpaho-mqttpp-dev (Ubuntu)")
        set(MQTT_TRANSPORT_AVAILABLE FALSE)
        set(ENABLE_MQTT_SINK OFF)
        set(ENABLE_SPARKPLUG_SINK OFF)
        set(ENABLE_SPARKPLUG_SCOOP OFF)
    endif()
endif()

# MQTT Sink
if((BUILD_ALL_COMPONENTS OR ENABLE_MQTT_SINK) AND MQTT_TRANSPORT_AVAILABLE)
    conditional_add_subdirectory(libipb-sink-mqtt ENABLE_MQTT_SINK)
    message(STATUS "MQTT sink enabled")
endif()

# Sparkplug B Sink
if((BUILD_ALL_COMPONENTS OR ENABLE_SPARKPLUG_SINK) AND MQTT_TRANSPORT_AVAILABLE)
    conditional_add_subdirectory(libipb-sink-sparkplug ENABLE_SPARKPLUG_SINK)
    message(STATUS "Sparkplug B sink enabled")
endif()

# Sparkplug B Scoop
if((BUILD_ALL_COMPONENTS OR ENABLE_SPARKPLUG_SCOOP) AND MQTT_TRANSPORT_AVAILABLE)
    conditional_add_subdirectory(libipb-scoop-sparkplug ENABLE_SPARKPLUG_SCOOP)
    message(STATUS "Sparkplug B scoop enabled")
endif()

# Main application
if(BUILD_ALL_COMPONENTS OR BUILD_GATE)
    conditional_add_subdirectory(ipb-gate BUILD_GATE)
endif()

# Examples
if(BUILD_EXAMPLES)
    conditional_add_subdirectory(examples TRUE)
endif()

# Tests
if(BUILD_TESTING)
    enable_testing()
    conditional_add_subdirectory(tests TRUE)
endif()

# Create build info file
configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/build_info.hpp.in"
    "${CMAKE_CURRENT_BINARY_DIR}/include/ipb/build_info.hpp"
    @ONLY
)

# Install build info
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/include/ipb/build_info.hpp"
    DESTINATION include/ipb
)

# Configuration summary
message(STATUS "")
message(STATUS "IPB Configuration Summary:")
message(STATUS "  Version: ${PROJECT_VERSION}")
message(STATUS "  Build type: ${CMAKE_BUILD_TYPE}")
message(STATUS "  C++ standard: ${CMAKE_CXX_STANDARD}")
message(STATUS "  Platform: ${CMAKE_SYSTEM_NAME}")
message(STATUS "  Compiler: ${CMAKE_CXX_COMPILER_ID}")
if(PLATFORM_MACOS)
    message(STATUS "  Homebrew prefix: ${CMAKE_PREFIX_PATH}")
endif()
message(STATUS "")
message(STATUS "Components:")
message(STATUS "  Common library: ${BUILD_COMMON}")
message(STATUS "  Router: ${BUILD_ROUTER}")
message(STATUS "  IPB Gate: ${BUILD_GATE}")
message(STATUS "")
message(STATUS "Transport:")
message(STATUS "  MQTT transport: ${MQTT_TRANSPORT_AVAILABLE}")
message(STATUS "")
message(STATUS "Sinks:")
message(STATUS "  Console sink: ${ENABLE_CONSOLE_SINK}")
message(STATUS "  Syslog sink: ${ENABLE_SYSLOG_SINK}")
message(STATUS "  MQTT sink: ${ENABLE_MQTT_SINK}")
message(STATUS "  Sparkplug B sink: ${ENABLE_SPARKPLUG_SINK}")
message(STATUS "")
message(STATUS "Scoops (Data Collectors):")
message(STATUS "  Modbus scoop: ${ENABLE_MODBUS_SCOOP}")
message(STATUS "  OPC UA scoop: ${ENABLE_OPCUA_SCOOP}")
message(STATUS "  Sparkplug B scoop: ${ENABLE_SPARKPLUG_SCOOP}")
message(STATUS "")
message(STATUS "Options:")
message(STATUS "  Examples: ${BUILD_EXAMPLES}")
message(STATUS "  Tests: ${BUILD_TESTING}")
message(STATUS "  Optimizations: ${ENABLE_OPTIMIZATIONS}")
message(STATUS "  LTO: ${ENABLE_LTO}")
message(STATUS "  Sanitizers: ${ENABLE_SANITIZERS}")
message(STATUS "  Coverage: ${ENABLE_COVERAGE}")
message(STATUS "")

# Print build instructions
if(CMAKE_GENERATOR STREQUAL "Unix Makefiles")
    if(PLATFORM_MACOS)
        set(NPROC_CMD "$(sysctl -n hw.ncpu)")
    else()
        set(NPROC_CMD "$(nproc)")
    endif()
    
    message(STATUS "Build with: make -j${NPROC_CMD}")
    message(STATUS "Install with: sudo make install")
    if(BUILD_TESTING)
        message(STATUS "Test with: ctest")
    endif()
endif()

message(STATUS "")

