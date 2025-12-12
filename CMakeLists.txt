# IPB - Industrial Protocol Bridge
# Restructured for modular distribution
cmake_minimum_required(VERSION 3.16)

project(IPB VERSION 1.5.0 LANGUAGES CXX)

# C++ Standard
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Include IPB build system modules
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(IPBOptions)
include(IPBDependencies)
include(IPBPrintConfig)

# ============================================================================
# BUILD OPTIONS
# ============================================================================

# Build profiles
option(IPB_EMBEDDED "Build for embedded systems (minimal footprint)" OFF)
option(IPB_FULL "Build all components" OFF)

# Development options
option(BUILD_TESTING "Build tests" OFF)
option(BUILD_EXAMPLES "Build examples" ON)
option(ENABLE_OPTIMIZATIONS "Enable performance optimizations" ON)
option(ENABLE_LTO "Enable Link Time Optimization" OFF)
option(ENABLE_SANITIZERS "Enable sanitizers (Debug builds)" OFF)
option(ENABLE_COVERAGE "Enable code coverage" OFF)

# Core components (always built)
set(BUILD_CORE ON)

# Sink options
option(IPB_SINK_CONSOLE "Enable Console sink" ON)
option(IPB_SINK_SYSLOG "Enable Syslog sink" ON)
option(IPB_SINK_MQTT "Enable MQTT sink" ON)
option(IPB_SINK_KAFKA "Enable Kafka sink" OFF)
option(IPB_SINK_SPARKPLUG "Enable Sparkplug B sink" OFF)
option(IPB_SINK_ZMQ "Enable ZeroMQ sink" OFF)

# Scoop options (data collectors)
option(IPB_SCOOP_CONSOLE "Enable Console scoop" OFF)
option(IPB_SCOOP_MODBUS "Enable Modbus scoop" OFF)
option(IPB_SCOOP_OPCUA "Enable OPC UA scoop" OFF)
option(IPB_SCOOP_MQTT "Enable MQTT scoop" ON)
option(IPB_SCOOP_SPARKPLUG "Enable Sparkplug B scoop" OFF)

# Transport options
option(IPB_TRANSPORT_MQTT "Build MQTT transport" ON)
option(IPB_TRANSPORT_HTTP "Build HTTP transport" ON)

# Application options
option(IPB_BUILD_GATE "Build ipb-gate application" ON)
option(IPB_BUILD_BRIDGE "Build ipb-bridge application" ON)

# ============================================================================
# BUILD PROFILES
# ============================================================================

if(IPB_EMBEDDED)
    message(STATUS "IPB: Embedded profile - minimal build")
    set(IPB_SINK_CONSOLE ON)
    set(IPB_SINK_SYSLOG OFF)
    set(IPB_SINK_MQTT OFF)
    set(IPB_SINK_KAFKA OFF)
    set(IPB_BUILD_GATE OFF)
    set(IPB_BUILD_BRIDGE ON)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Os")
    add_compile_definitions(IPB_EMBEDDED=1)
endif()

if(IPB_FULL)
    message(STATUS "IPB: Full profile - all components")
    set(IPB_SINK_CONSOLE ON)
    set(IPB_SINK_SYSLOG ON)
    set(IPB_SINK_MQTT ON)
    set(IPB_SINK_KAFKA ON)
    set(IPB_SINK_SPARKPLUG ON)
    set(IPB_SCOOP_MODBUS ON)
    set(IPB_SCOOP_OPCUA ON)
    set(IPB_SCOOP_MQTT ON)
    set(IPB_SCOOP_SPARKPLUG ON)
    set(IPB_TRANSPORT_MQTT ON)
    set(IPB_TRANSPORT_HTTP ON)
    set(IPB_BUILD_GATE ON)
    set(IPB_BUILD_BRIDGE ON)
endif()

# ============================================================================
# PLATFORM DETECTION
# ============================================================================

if(APPLE)
    set(PLATFORM_MACOS TRUE)
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

# ============================================================================
# DEPENDENCIES
# ============================================================================

find_package(PkgConfig REQUIRED)
find_package(Threads REQUIRED)

# Find jsoncpp
if(NOT TARGET jsoncpp_lib AND NOT TARGET JsonCpp::JsonCpp)
    find_package(jsoncpp QUIET)
    if(NOT jsoncpp_FOUND)
        pkg_check_modules(JSONCPP REQUIRED jsoncpp)
        add_library(jsoncpp_lib INTERFACE IMPORTED)
        set_target_properties(jsoncpp_lib PROPERTIES
            INTERFACE_INCLUDE_DIRECTORIES "${JSONCPP_INCLUDE_DIRS}"
            INTERFACE_LINK_LIBRARIES "${JSONCPP_LIBRARIES}"
        )
    endif()
endif()

# Find YAML-CPP
find_package(yaml-cpp QUIET)
if(NOT yaml-cpp_FOUND)
    pkg_check_modules(YAML_CPP REQUIRED yaml-cpp)
endif()

# ============================================================================
# COMPILER FLAGS
# ============================================================================

if(ENABLE_OPTIMIZATIONS)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra -Wpedantic")
    set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -O3")
    if(NOT APPLE)
        set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -march=native")
    endif()
    set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -g -O0")
endif()

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

if(ENABLE_SANITIZERS AND CMAKE_BUILD_TYPE STREQUAL "Debug")
    set(SANITIZER_FLAGS "-fsanitize=address -fsanitize=undefined -fno-omit-frame-pointer")
    set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} ${SANITIZER_FLAGS}")
    set(CMAKE_LINKER_FLAGS_DEBUG "${CMAKE_LINKER_FLAGS_DEBUG} ${SANITIZER_FLAGS}")
    message(STATUS "Sanitizers enabled for Debug builds")
endif()

if(ENABLE_COVERAGE AND CMAKE_BUILD_TYPE STREQUAL "Debug")
    set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} --coverage")
    set(CMAKE_LINKER_FLAGS_DEBUG "${CMAKE_LINKER_FLAGS_DEBUG} --coverage")
    message(STATUS "Code coverage enabled")
endif()

# ============================================================================
# GLOBAL INCLUDE DIRECTORIES
# ============================================================================

include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}/core/common/include
)

# ============================================================================
# BUILD COMPONENTS
# ============================================================================

# Helper function
function(ipb_add_subdirectory dir condition)
    if(${condition})
        if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/${dir}/CMakeLists.txt")
            add_subdirectory(${dir})
            message(STATUS "Building: ${dir}")
        else()
            message(WARNING "${dir} requested but CMakeLists.txt not found")
        endif()
    else()
        message(STATUS "Skipping: ${dir}")
    endif()
endfunction()

# --- CORE (always built) ---
add_subdirectory(core)

# --- TRANSPORT LAYERS ---
# Check MQTT transport dependencies
set(MQTT_TRANSPORT_AVAILABLE FALSE)
if(IPB_TRANSPORT_MQTT OR IPB_SINK_MQTT OR IPB_SCOOP_MQTT OR IPB_SINK_SPARKPLUG OR IPB_SCOOP_SPARKPLUG)
    find_library(PAHO_MQTT_CPP_LIB paho-mqttpp3)
    find_library(PAHO_MQTT_C_LIB paho-mqtt3as)
    if(PAHO_MQTT_CPP_LIB AND PAHO_MQTT_C_LIB)
        set(MQTT_TRANSPORT_AVAILABLE TRUE)
        ipb_add_subdirectory(transport/mqtt TRUE)
    else()
        message(WARNING "MQTT libraries not found. MQTT components disabled.")
        set(IPB_SINK_MQTT OFF)
        set(IPB_SCOOP_MQTT OFF)
        set(IPB_SINK_SPARKPLUG OFF)
        set(IPB_SCOOP_SPARKPLUG OFF)
    endif()
endif()

# Check HTTP transport dependencies
set(HTTP_TRANSPORT_AVAILABLE FALSE)
if(IPB_TRANSPORT_HTTP)
    find_package(CURL QUIET)
    if(NOT CURL_FOUND)
        find_library(CURL_LIB curl)
    endif()
    if(CURL_FOUND OR CURL_LIB)
        set(HTTP_TRANSPORT_AVAILABLE TRUE)
        ipb_add_subdirectory(transport/http TRUE)
    else()
        message(STATUS "libcurl not found. HTTP transport disabled.")
    endif()
endif()

# --- SINKS ---
ipb_add_subdirectory(sinks/console IPB_SINK_CONSOLE)
ipb_add_subdirectory(sinks/syslog IPB_SINK_SYSLOG)

if(MQTT_TRANSPORT_AVAILABLE)
    ipb_add_subdirectory(sinks/mqtt IPB_SINK_MQTT)
    ipb_add_subdirectory(sinks/sparkplug IPB_SINK_SPARKPLUG)
endif()

ipb_add_subdirectory(sinks/kafka IPB_SINK_KAFKA)
ipb_add_subdirectory(sinks/zmq IPB_SINK_ZMQ)

# --- SCOOPS ---
ipb_add_subdirectory(scoops/console IPB_SCOOP_CONSOLE)
ipb_add_subdirectory(scoops/modbus IPB_SCOOP_MODBUS)
ipb_add_subdirectory(scoops/opcua IPB_SCOOP_OPCUA)

if(MQTT_TRANSPORT_AVAILABLE)
    ipb_add_subdirectory(scoops/mqtt IPB_SCOOP_MQTT)
    ipb_add_subdirectory(scoops/sparkplug IPB_SCOOP_SPARKPLUG)
endif()

# --- APPLICATIONS ---
# Use apps aggregator if it exists, otherwise add individual apps
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/apps/CMakeLists.txt")
    add_subdirectory(apps)
else()
    ipb_add_subdirectory(apps/ipb-gate IPB_BUILD_GATE)
    ipb_add_subdirectory(apps/ipb-bridge IPB_BUILD_BRIDGE)
endif()

# --- EXAMPLES & TESTS ---
if(BUILD_EXAMPLES)
    ipb_add_subdirectory(examples TRUE)
endif()

if(BUILD_TESTING)
    enable_testing()
    ipb_add_subdirectory(tests TRUE)
endif()

# ============================================================================
# INSTALL & EXPORT
# ============================================================================

include(CMakePackageConfigHelpers)
include(GNUInstallDirs)

# Configure build info headers
configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/build_info.hpp.in"
    "${CMAKE_CURRENT_BINARY_DIR}/include/ipb/build_info.hpp"
    @ONLY
)

configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/build_config.hpp.in"
    "${CMAKE_CURRENT_BINARY_DIR}/include/ipb/common/build_config.hpp"
    @ONLY
)

# Install build headers
install(FILES
    "${CMAKE_CURRENT_BINARY_DIR}/include/ipb/build_info.hpp"
    DESTINATION include/ipb
)
install(FILES
    "${CMAKE_CURRENT_BINARY_DIR}/include/ipb/common/build_config.hpp"
    DESTINATION include/ipb/common
)

# Add generated include directory
include_directories(${CMAKE_CURRENT_BINARY_DIR}/include)

# ============================================================================
# CONFIGURATION SUMMARY
# ============================================================================

ipb_print_config()

if(CMAKE_GENERATOR STREQUAL "Unix Makefiles")
    if(PLATFORM_MACOS)
        set(NPROC_CMD "$(sysctl -n hw.ncpu)")
    else()
        set(NPROC_CMD "$(nproc)")
    endif()
    message(STATUS "Build with: make -j${NPROC_CMD}")
    if(BUILD_TESTING)
        message(STATUS "Test with: ctest")
    endif()
endif()

message(STATUS "")
