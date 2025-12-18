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

# Component profiles (WHAT to build)
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
# BUILD PROFILES (based on IPB_BUILD_MODE from cmake/IPBOptions.cmake)
# ============================================================================

# Apply mode-specific component defaults
if(IPB_BUILD_MODE STREQUAL "EMBEDDED")
    message(STATUS "IPB: EMBEDDED mode - minimal components")
    set(IPB_SINK_CONSOLE ON)
    set(IPB_SINK_SYSLOG OFF)
    set(IPB_SINK_MQTT OFF)
    set(IPB_SINK_KAFKA OFF)
    set(IPB_BUILD_GATE OFF)
    set(IPB_BUILD_BRIDGE ON)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Os")
elseif(IPB_BUILD_MODE STREQUAL "EDGE")
    message(STATUS "IPB: EDGE mode - balanced components")
    set(IPB_SINK_CONSOLE ON)
    set(IPB_SINK_SYSLOG ON)
    set(IPB_SINK_MQTT ON)
    set(IPB_SINK_KAFKA OFF)
    set(IPB_BUILD_GATE ON)
    set(IPB_BUILD_BRIDGE ON)
endif()

# IPB_FULL overrides mode defaults with all components
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

# Option to control auto-fallback behavior
option(IPB_AUTO_FALLBACK_EMBEDDED "Auto-fallback to EMBEDDED mode if dependencies missing" ON)

# Helper function to find jsoncpp
function(ipb_find_jsoncpp OUT_FOUND)
    set(${OUT_FOUND} FALSE PARENT_SCOPE)

    # Method 1: CMake find_package
    find_package(jsoncpp CONFIG QUIET)
    if(jsoncpp_FOUND OR TARGET JsonCpp::JsonCpp)
        if(TARGET JsonCpp::JsonCpp AND NOT TARGET jsoncpp_lib)
            add_library(jsoncpp_lib ALIAS JsonCpp::JsonCpp)
        endif()
        message(STATUS "Found jsoncpp via CMake config")
        set(${OUT_FOUND} TRUE PARENT_SCOPE)
        return()
    endif()

    # Method 2: Try find_path and find_library
    find_path(JSONCPP_INCLUDE_DIR json/json.h
        PATHS /usr/include /usr/local/include
        PATH_SUFFIXES jsoncpp
    )
    find_library(JSONCPP_LIBRARY
        NAMES jsoncpp libjsoncpp
        PATHS /usr/lib /usr/local/lib /usr/lib/x86_64-linux-gnu
    )
    if(JSONCPP_INCLUDE_DIR AND JSONCPP_LIBRARY)
        if(NOT TARGET jsoncpp_lib)
            add_library(jsoncpp_lib UNKNOWN IMPORTED)
            set_target_properties(jsoncpp_lib PROPERTIES
                IMPORTED_LOCATION "${JSONCPP_LIBRARY}"
                INTERFACE_INCLUDE_DIRECTORIES "${JSONCPP_INCLUDE_DIR}"
            )
        endif()
        message(STATUS "Found jsoncpp: ${JSONCPP_LIBRARY}")
        set(${OUT_FOUND} TRUE PARENT_SCOPE)
        return()
    endif()

    # Method 3: Try pkg-config
    pkg_check_modules(JSONCPP_PKG QUIET jsoncpp)
    if(JSONCPP_PKG_FOUND)
        if(NOT TARGET jsoncpp_lib)
            add_library(jsoncpp_lib INTERFACE IMPORTED)
            set_target_properties(jsoncpp_lib PROPERTIES
                INTERFACE_INCLUDE_DIRECTORIES "${JSONCPP_PKG_INCLUDE_DIRS}"
                INTERFACE_LINK_LIBRARIES "${JSONCPP_PKG_LIBRARIES}"
            )
        endif()
        message(STATUS "Found jsoncpp via pkg-config")
        set(${OUT_FOUND} TRUE PARENT_SCOPE)
        return()
    endif()
endfunction()

# Helper function to find yaml-cpp
function(ipb_find_yaml_cpp OUT_FOUND)
    set(${OUT_FOUND} FALSE PARENT_SCOPE)

    # Method 1: CMake find_package (CONFIG mode)
    find_package(yaml-cpp CONFIG QUIET)
    if(yaml-cpp_FOUND)
        message(STATUS "Found yaml-cpp via CMake config")
        set(${OUT_FOUND} TRUE PARENT_SCOPE)
        return()
    endif()

    # Method 2: Try find_path and find_library
    find_path(YAML_CPP_INCLUDE_DIR yaml-cpp/yaml.h
        PATHS /usr/include /usr/local/include
    )
    find_library(YAML_CPP_LIBRARY
        NAMES yaml-cpp libyaml-cpp
        PATHS /usr/lib /usr/local/lib /usr/lib/x86_64-linux-gnu
    )
    if(YAML_CPP_INCLUDE_DIR AND YAML_CPP_LIBRARY)
        if(NOT TARGET yaml-cpp)
            add_library(yaml-cpp UNKNOWN IMPORTED)
            set_target_properties(yaml-cpp PROPERTIES
                IMPORTED_LOCATION "${YAML_CPP_LIBRARY}"
                INTERFACE_INCLUDE_DIRECTORIES "${YAML_CPP_INCLUDE_DIR}"
            )
        endif()
        message(STATUS "Found yaml-cpp: ${YAML_CPP_LIBRARY}")
        set(${OUT_FOUND} TRUE PARENT_SCOPE)
        return()
    endif()

    # Method 3: Try pkg-config
    pkg_check_modules(YAML_CPP_PKG QUIET yaml-cpp)
    if(YAML_CPP_PKG_FOUND)
        if(NOT TARGET yaml-cpp)
            add_library(yaml-cpp INTERFACE IMPORTED)
            set_target_properties(yaml-cpp PROPERTIES
                INTERFACE_INCLUDE_DIRECTORIES "${YAML_CPP_PKG_INCLUDE_DIRS}"
                INTERFACE_LINK_LIBRARIES "${YAML_CPP_PKG_LIBRARIES}"
            )
        endif()
        message(STATUS "Found yaml-cpp via pkg-config")
        set(${OUT_FOUND} TRUE PARENT_SCOPE)
        return()
    endif()
endfunction()

# Dependency detection and auto-fallback logic
set(IPB_MISSING_DEPS "")

if(NOT IPB_BUILD_MODE STREQUAL "EMBEDDED")
    # Check for jsoncpp
    if(NOT TARGET jsoncpp_lib AND NOT TARGET JsonCpp::JsonCpp)
        ipb_find_jsoncpp(JSONCPP_FOUND)
        if(NOT JSONCPP_FOUND)
            list(APPEND IPB_MISSING_DEPS "jsoncpp")
        endif()
    endif()

    # Check for yaml-cpp
    ipb_find_yaml_cpp(YAML_CPP_FOUND)
    if(NOT YAML_CPP_FOUND)
        list(APPEND IPB_MISSING_DEPS "yaml-cpp")
    endif()

    # Handle missing dependencies
    if(IPB_MISSING_DEPS)
        if(IPB_AUTO_FALLBACK_EMBEDDED)
            message(WARNING
                "======================================================================\n"
                "  AUTOMATIC FALLBACK TO EMBEDDED MODE\n"
                "======================================================================\n"
                "  Missing dependencies: ${IPB_MISSING_DEPS}\n"
                "\n"
                "  The build will continue in EMBEDDED mode with reduced functionality.\n"
                "  To use full ${IPB_BUILD_MODE} mode, install the missing libraries:\n"
                "\n"
                "  Ubuntu/Debian:\n"
                "    sudo apt-get install libjsoncpp-dev libyaml-cpp-dev\n"
                "\n"
                "  Fedora/RHEL:\n"
                "    sudo dnf install jsoncpp-devel yaml-cpp-devel\n"
                "\n"
                "  macOS:\n"
                "    brew install jsoncpp yaml-cpp\n"
                "\n"
                "  To disable auto-fallback and see errors:\n"
                "    cmake -DIPB_AUTO_FALLBACK_EMBEDDED=OFF ...\n"
                "======================================================================")

            # Switch to EMBEDDED mode
            set(IPB_BUILD_MODE "EMBEDDED")
            set(IPB_AUTO_FALLBACK_ACTIVATED TRUE)

            # Apply EMBEDDED mode settings
            message(STATUS "IPB: EMBEDDED mode - minimal components (auto-fallback)")
            set(IPB_SINK_CONSOLE ON)
            set(IPB_SINK_SYSLOG OFF)
            set(IPB_SINK_MQTT OFF)
            set(IPB_SINK_KAFKA OFF)
            set(IPB_BUILD_GATE OFF)
            set(IPB_BUILD_BRIDGE ON)
            set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Os")
        else()
            # No auto-fallback, show error with instructions
            message(FATAL_ERROR
                "Missing required dependencies: ${IPB_MISSING_DEPS}\n"
                "\n"
                "Install them via:\n"
                "  Ubuntu/Debian: sudo apt-get install libjsoncpp-dev libyaml-cpp-dev\n"
                "  Fedora/RHEL:   sudo dnf install jsoncpp-devel yaml-cpp-devel\n"
                "  macOS:         brew install jsoncpp yaml-cpp\n"
                "\n"
                "Or use EMBEDDED mode:\n"
                "  cmake -DIPB_BUILD_MODE=EMBEDDED ...\n"
                "\n"
                "Or enable auto-fallback (default):\n"
                "  cmake -DIPB_AUTO_FALLBACK_EMBEDDED=ON ...")
        endif()
    endif()
endif()

# EMBEDDED mode: use cJSON instead
if(IPB_BUILD_MODE STREQUAL "EMBEDDED")
    pkg_check_modules(CJSON QUIET cjson)
    if(CJSON_FOUND)
        message(STATUS "EMBEDDED mode: using cJSON")
    endif()
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

# --- BENCHMARKS ---
option(IPB_BUILD_BENCHMARKS "Build IPB benchmark suite" ON)
if(IPB_BUILD_BENCHMARKS)
    add_subdirectory(benchmarks)
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
