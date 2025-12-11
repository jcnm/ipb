# Sparkplug B Sink Library
# Data publisher for Sparkplug B protocol over MQTT
#
# Dependencies:
# - libipb-common (core data structures)
# - libipb-transport-mqtt (shared MQTT client)
# - protobuf (Sparkplug payload encoding)

cmake_minimum_required(VERSION 3.16)

project(libipb-sink-sparkplug VERSION 1.0.0)

# C++ Standard
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Find dependencies
find_package(Protobuf QUIET)

# Source files
set(SPARKPLUG_SINK_SOURCES
    src/sparkplug_sink.cpp
    src/sparkplug_encoder.cpp
)

# Protobuf sources (shared with scoop if both are built)
if(Protobuf_FOUND AND NOT TARGET sparkplug_proto)
    set(SPARKPLUG_PROTO_FILES
        proto/sparkplug_b.proto
    )

    protobuf_generate_cpp(PROTO_SRCS PROTO_HDRS ${SPARKPLUG_PROTO_FILES})
    list(APPEND SPARKPLUG_SINK_SOURCES ${PROTO_SRCS})
endif()

# Create shared library
add_library(ipb-sink-sparkplug SHARED ${SPARKPLUG_SINK_SOURCES})

# Include directories
target_include_directories(ipb-sink-sparkplug
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
    PRIVATE
        ${CMAKE_CURRENT_BINARY_DIR}
)

# Link dependencies
target_link_libraries(ipb-sink-sparkplug
    PUBLIC
        ipb-common
        ipb-transport-mqtt
    PRIVATE
        pthread
)

if(Protobuf_FOUND)
    target_link_libraries(ipb-sink-sparkplug PRIVATE ${Protobuf_LIBRARIES})
    target_include_directories(ipb-sink-sparkplug PRIVATE ${Protobuf_INCLUDE_DIRS})
    target_compile_definitions(ipb-sink-sparkplug PRIVATE IPB_HAS_PROTOBUF=1)
endif()

# Compiler options
target_compile_options(ipb-sink-sparkplug PRIVATE
    -Wall -Wextra -Wpedantic
    -fPIC
)

if(CMAKE_BUILD_TYPE STREQUAL "Release")
    target_compile_options(ipb-sink-sparkplug PRIVATE -O3 -march=native)
endif()

# Set library properties
set_target_properties(ipb-sink-sparkplug PROPERTIES
    VERSION ${PROJECT_VERSION}
    SOVERSION 1
    OUTPUT_NAME ipb-sink-sparkplug
)

# Install targets
install(TARGETS ipb-sink-sparkplug
    LIBRARY DESTINATION lib
    ARCHIVE DESTINATION lib
)

install(DIRECTORY include/
    DESTINATION include
    FILES_MATCHING PATTERN "*.hpp"
)

# Alias for use in parent project
add_library(ipb::sink-sparkplug ALIAS ipb-sink-sparkplug)
