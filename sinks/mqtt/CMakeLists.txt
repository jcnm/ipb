# MQTT Sink Library
cmake_minimum_required(VERSION 3.16)

project(libipb-sink-mqtt VERSION 1.3.0)

# C++ Standard
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Find dependencies
find_package(PkgConfig REQUIRED)
# Find jsoncpp (avoid conflicts)
if(NOT TARGET jsoncpp_lib AND NOT TARGET JsonCpp::JsonCpp)
    find_package(jsoncpp QUIET)
    if(NOT jsoncpp_FOUND)
        pkg_check_modules(JSONCPP REQUIRED jsoncpp)
    endif()
endif()
find_package(ZLIB REQUIRED)

# Find Paho MQTT C++ library
find_library(PAHO_MQTT_CPP_LIB paho-mqttpp3)
find_library(PAHO_MQTT_C_LIB paho-mqtt3as)

if(NOT PAHO_MQTT_CPP_LIB OR NOT PAHO_MQTT_C_LIB)
    message(WARNING "Paho MQTT libraries not found. MQTT sink will be disabled.")
    return()
endif()

# Include directories
include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/core/common/include
    ${CMAKE_SOURCE_DIR}/transport/mqtt/include
)

# Source files
set(MQTT_SINK_SOURCES
    src/mqtt_sink.cpp
)

# Create shared library
add_library(ipb-sink-mqtt SHARED ${MQTT_SINK_SOURCES})

# Link libraries - uses shared transport layer
target_link_libraries(ipb-sink-mqtt
    ipb-common
    ipb-transport-mqtt
    jsoncpp_lib
    ZLIB::ZLIB
    pthread
)

# Compiler flags
target_compile_options(ipb-sink-mqtt PRIVATE
    -Wall -Wextra -Wpedantic
    -O3 -march=native
    -fPIC
)

# Preprocessor definitions
target_compile_definitions(ipb-sink-mqtt PRIVATE
    MQTT_SINK_VERSION_MAJOR=1
    MQTT_SINK_VERSION_MINOR=3
    MQTT_SINK_VERSION_PATCH=0
)

# Install targets
install(TARGETS ipb-sink-mqtt
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
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/libipb-sink-mqtt-config.cmake.in"
    "${CMAKE_CURRENT_BINARY_DIR}/libipb-sink-mqtt-config.cmake"
    INSTALL_DESTINATION lib/cmake/libipb-sink-mqtt
    NO_CHECK_REQUIRED_COMPONENTS_MACRO
)

write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/libipb-sink-mqtt-config-version.cmake"
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion
)

install(FILES
    "${CMAKE_CURRENT_BINARY_DIR}/libipb-sink-mqtt-config.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/libipb-sink-mqtt-config-version.cmake"
    DESTINATION lib/cmake/libipb-sink-mqtt
)

# Tests
if(BUILD_TESTING AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tests")
    add_subdirectory(tests)
endif()

# Examples
if(BUILD_EXAMPLES)
    add_subdirectory(examples)
endif()

