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
# Security Note: Minimum versions specified to avoid known CVEs
# Review and update these versions periodically

# PkgConfig is optional on Windows (vcpkg uses CMake's native find_package)
if(NOT WIN32)
    find_package(PkgConfig REQUIRED)
else()
    find_package(PkgConfig QUIET)
endif()
find_package(Threads REQUIRED)

# OpenSSL - Required for cryptographic operations (SHA-256, TLS, CSPRNG)
# Minimum version 1.1.1 for TLS 1.3 support, prefer 3.x for active support
find_package(OpenSSL 1.1.1 REQUIRED)
if(OpenSSL_FOUND)
    message(STATUS "OpenSSL version: ${OPENSSL_VERSION}")
    if(OPENSSL_VERSION VERSION_LESS "3.0.0")
        message(WARNING "OpenSSL ${OPENSSL_VERSION} is EOL. Consider upgrading to OpenSSL 3.x")
    endif()
endif()

# Option to control auto-fallback behavior
option(IPB_AUTO_FALLBACK_EMBEDDED "Auto-fallback to EMBEDDED mode if dependencies missing" ON)

# Helper function to find jsoncpp
# Note: Prefer version >= 1.9.4 to avoid CVE-2022-40734 (DoS via crafted JSON)
function(ipb_find_jsoncpp OUT_FOUND)
    set(${OUT_FOUND} FALSE PARENT_SCOPE)

    # Method 1: CMake find_package
    find_package(jsoncpp CONFIG QUIET)
    if(jsoncpp_FOUND OR TARGET JsonCpp::JsonCpp)
        if(TARGET JsonCpp::JsonCpp AND NOT TARGET jsoncpp_lib)
            add_library(jsoncpp_lib ALIAS JsonCpp::JsonCpp)
        endif()
        # On macOS with Homebrew, ensure include and library directories are set
        if(APPLE)
            set(_jsoncpp_target "")
            if(TARGET JsonCpp::JsonCpp)
                set(_jsoncpp_target JsonCpp::JsonCpp)
            elseif(TARGET jsoncpp_lib)
                set(_jsoncpp_target jsoncpp_lib)
            endif()
            if(_jsoncpp_target)
                get_target_property(_jsoncpp_loc ${_jsoncpp_target} IMPORTED_LOCATION)
                if(NOT _jsoncpp_loc)
                    get_target_property(_jsoncpp_loc ${_jsoncpp_target} IMPORTED_LOCATION_RELEASE)
                endif()
                if(NOT _jsoncpp_loc)
                    get_target_property(_jsoncpp_loc ${_jsoncpp_target} IMPORTED_LOCATION_NOCONFIG)
                endif()
                if(_jsoncpp_loc)
                    get_filename_component(_jsoncpp_dir "${_jsoncpp_loc}" DIRECTORY)
                    get_filename_component(_jsoncpp_prefix "${_jsoncpp_dir}" DIRECTORY)
                    set_property(TARGET ${_jsoncpp_target} APPEND PROPERTY INTERFACE_LINK_DIRECTORIES "${_jsoncpp_dir}")
                    # Also ensure include directories are set (Homebrew uses /opt/homebrew/include)
                    if(EXISTS "${_jsoncpp_prefix}/include/json/json.h")
                        set_property(TARGET ${_jsoncpp_target} APPEND PROPERTY INTERFACE_INCLUDE_DIRECTORIES "${_jsoncpp_prefix}/include")
                        message(STATUS "  Added jsoncpp include directory: ${_jsoncpp_prefix}/include")
                    elseif(EXISTS "${_jsoncpp_prefix}/include/jsoncpp/json/json.h")
                        set_property(TARGET ${_jsoncpp_target} APPEND PROPERTY INTERFACE_INCLUDE_DIRECTORIES "${_jsoncpp_prefix}/include/jsoncpp")
                        message(STATUS "  Added jsoncpp include directory: ${_jsoncpp_prefix}/include/jsoncpp")
                    endif()
                    message(STATUS "  Added jsoncpp link directory: ${_jsoncpp_dir}")
                endif()
            endif()
        endif()
        message(STATUS "Found jsoncpp via CMake config")
        set(${OUT_FOUND} TRUE PARENT_SCOPE)
        return()
    endif()

    # Method 2: Try find_path and find_library
    find_path(JSONCPP_INCLUDE_DIR json/json.h
        PATHS /usr/include /usr/local/include /opt/homebrew/include
        PATH_SUFFIXES jsoncpp
    )
    find_library(JSONCPP_LIBRARY
        NAMES jsoncpp libjsoncpp
        PATHS /usr/lib /usr/local/lib /usr/lib/x86_64-linux-gnu /opt/homebrew/lib
    )
    if(JSONCPP_INCLUDE_DIR AND JSONCPP_LIBRARY)
        if(NOT TARGET jsoncpp_lib)
            add_library(jsoncpp_lib UNKNOWN IMPORTED)
            get_filename_component(JSONCPP_LIBRARY_DIR "${JSONCPP_LIBRARY}" DIRECTORY)
            set_target_properties(jsoncpp_lib PROPERTIES
                IMPORTED_LOCATION "${JSONCPP_LIBRARY}"
                INTERFACE_INCLUDE_DIRECTORIES "${JSONCPP_INCLUDE_DIR}"
                INTERFACE_LINK_DIRECTORIES "${JSONCPP_LIBRARY_DIR}"
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
                INTERFACE_LINK_DIRECTORIES "${JSONCPP_PKG_LIBRARY_DIRS}"
            )
        endif()
        message(STATUS "Found jsoncpp via pkg-config: ${JSONCPP_PKG_LIBRARY_DIRS}")
        set(${OUT_FOUND} TRUE PARENT_SCOPE)
        return()
    endif()
endfunction()

# Helper function to find yaml-cpp
# Note: Prefer version >= 0.7.0 for security fixes
function(ipb_find_yaml_cpp OUT_FOUND)
    set(${OUT_FOUND} FALSE PARENT_SCOPE)

    # Method 1: CMake find_package (CONFIG mode)
    find_package(yaml-cpp CONFIG QUIET)
    if(yaml-cpp_FOUND)
        message(STATUS "Found yaml-cpp via CMake config")

        # Modern yaml-cpp (0.8+) uses yaml-cpp::yaml-cpp target name
        # Create an alias if only the namespaced target exists
        if(TARGET yaml-cpp::yaml-cpp AND NOT TARGET yaml-cpp)
            # Create an interface library that links to the namespaced target
            add_library(yaml-cpp INTERFACE IMPORTED)
            target_link_libraries(yaml-cpp INTERFACE yaml-cpp::yaml-cpp)
            message(STATUS "  Created yaml-cpp alias for yaml-cpp::yaml-cpp")
        endif()

        # On macOS with Homebrew, we need to ensure include and library directories are set
        # The yaml-cpp CMake config may not set these properly
        if(APPLE AND TARGET yaml-cpp)
            get_target_property(_yaml_cpp_loc yaml-cpp IMPORTED_LOCATION)
            if(NOT _yaml_cpp_loc)
                get_target_property(_yaml_cpp_loc yaml-cpp IMPORTED_LOCATION_RELEASE)
            endif()
            if(NOT _yaml_cpp_loc)
                get_target_property(_yaml_cpp_loc yaml-cpp IMPORTED_LOCATION_NOCONFIG)
            endif()
            if(_yaml_cpp_loc)
                get_filename_component(_yaml_cpp_dir "${_yaml_cpp_loc}" DIRECTORY)
                get_filename_component(_yaml_cpp_prefix "${_yaml_cpp_dir}" DIRECTORY)
                set_property(TARGET yaml-cpp APPEND PROPERTY INTERFACE_LINK_DIRECTORIES "${_yaml_cpp_dir}")
                # Also ensure include directories are set
                if(EXISTS "${_yaml_cpp_prefix}/include/yaml-cpp/yaml.h")
                    set_property(TARGET yaml-cpp APPEND PROPERTY INTERFACE_INCLUDE_DIRECTORIES "${_yaml_cpp_prefix}/include")
                    message(STATUS "  Added yaml-cpp include directory: ${_yaml_cpp_prefix}/include")
                endif()
                message(STATUS "  Added yaml-cpp link directory: ${_yaml_cpp_dir}")
            endif()
        endif()
        set(${OUT_FOUND} TRUE PARENT_SCOPE)
        return()
    endif()

    # Method 2: Try find_path and find_library
    find_path(YAML_CPP_INCLUDE_DIR yaml-cpp/yaml.h
        PATHS /usr/include /usr/local/include /opt/homebrew/include
    )
    find_library(YAML_CPP_LIBRARY
        NAMES yaml-cpp libyaml-cpp
        PATHS /usr/lib /usr/local/lib /usr/lib/x86_64-linux-gnu /opt/homebrew/lib
    )
    if(YAML_CPP_INCLUDE_DIR AND YAML_CPP_LIBRARY)
        if(NOT TARGET yaml-cpp)
            add_library(yaml-cpp UNKNOWN IMPORTED)
            get_filename_component(YAML_CPP_LIBRARY_DIR "${YAML_CPP_LIBRARY}" DIRECTORY)
            set_target_properties(yaml-cpp PROPERTIES
                IMPORTED_LOCATION "${YAML_CPP_LIBRARY}"
                INTERFACE_INCLUDE_DIRECTORIES "${YAML_CPP_INCLUDE_DIR}"
                INTERFACE_LINK_DIRECTORIES "${YAML_CPP_LIBRARY_DIR}"
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
                INTERFACE_LINK_DIRECTORIES "${YAML_CPP_PKG_LIBRARY_DIRS}"
            )
        endif()
        message(STATUS "Found yaml-cpp via pkg-config: ${YAML_CPP_PKG_LIBRARY_DIRS}")
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
    # Warning flags are compiler-specific
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra -Wpedantic")
        set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -O3")
        if(NOT APPLE)
            set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -march=native")
        endif()
        set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -g -O0")
    elseif(MSVC)
        set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /W4")
        set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} /O2")
        set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} /Zi /Od")
    endif()
endif()

# ============================================================================
# SECURITY HARDENING FLAGS
# ============================================================================
# These flags provide defense-in-depth against common attacks
option(ENABLE_SECURITY_FLAGS "Enable security hardening compiler flags" ON)

if(ENABLE_SECURITY_FLAGS)
    # Check if compiler supports security flags
    include(CheckCXXCompilerFlag)

    # Stack protection (canaries to detect buffer overflows)
    check_cxx_compiler_flag("-fstack-protector-strong" HAS_STACK_PROTECTOR)
    if(HAS_STACK_PROTECTOR)
        set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fstack-protector-strong")
    endif()

    # Format string vulnerability detection
    check_cxx_compiler_flag("-Wformat-security" HAS_FORMAT_SECURITY)
    if(HAS_FORMAT_SECURITY)
        set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wformat -Wformat-security")
    endif()

    # Buffer overflow detection (requires optimization)
    if(NOT CMAKE_BUILD_TYPE STREQUAL "Debug")
        add_definitions(-D_FORTIFY_SOURCE=2)
    endif()

    # Position Independent Executable (ASLR support)
    set(CMAKE_POSITION_INDEPENDENT_CODE ON)

    # Linker security flags (Linux only)
    if(UNIX AND NOT APPLE)
        # Read-only relocations
        set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -Wl,-z,relro")
        # Immediate binding (resolve all symbols at load time)
        set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -Wl,-z,now")
        # Non-executable stack
        set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -Wl,-z,noexecstack")
    endif()

    message(STATUS "Security hardening flags enabled")
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
    # First try CMake config (preferred for Windows/vcpkg)
    find_package(PahoMqttCpp QUIET CONFIG)
    if(PahoMqttCpp_FOUND)
        set(MQTT_TRANSPORT_AVAILABLE TRUE)
        ipb_add_subdirectory(transport/mqtt TRUE)
    else()
        # Fallback to manual library search
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
# Syslog is only available on POSIX systems (not Windows)
if(NOT WIN32)
    ipb_add_subdirectory(sinks/syslog IPB_SINK_SYSLOG)
endif()

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
