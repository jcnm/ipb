# Sparkplug B Scoop Library
# Data collector for Sparkplug B protocol over MQTT
#
# Dependencies:
# - libipb-common (core data structures)
# - libipb-transport-mqtt (shared MQTT client)
# - protobuf (Sparkplug payload encoding)

cmake_minimum_required(VERSION 3.16)

project(libipb-scoop-sparkplug VERSION 1.0.0)

# C++ Standard
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Find dependencies
find_package(Protobuf QUIET)

# Source files
set(SPARKPLUG_SCOOP_SOURCES
    src/sparkplug_scoop.cpp
    src/sparkplug_payload.cpp
    src/sparkplug_topic.cpp
)

# Protobuf sources (Sparkplug B payload definition)
if(Protobuf_FOUND)
    set(SPARKPLUG_PROTO_FILES
        proto/sparkplug_b.proto
    )

    protobuf_generate_cpp(PROTO_SRCS PROTO_HDRS ${SPARKPLUG_PROTO_FILES})
    list(APPEND SPARKPLUG_SCOOP_SOURCES ${PROTO_SRCS})
endif()

# Create shared library
add_library(ipb-scoop-sparkplug SHARED ${SPARKPLUG_SCOOP_SOURCES})

# Include directories
target_include_directories(ipb-scoop-sparkplug
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
    PRIVATE
        ${CMAKE_CURRENT_BINARY_DIR}  # For generated protobuf headers
)

# Link dependencies
target_link_libraries(ipb-scoop-sparkplug
    PUBLIC
        ipb-common
        ipb-transport-mqtt
)
if(NOT WIN32)
    target_link_libraries(ipb-scoop-sparkplug PRIVATE pthread)
endif()

if(Protobuf_FOUND)
    target_link_libraries(ipb-scoop-sparkplug PRIVATE ${Protobuf_LIBRARIES})
    target_include_directories(ipb-scoop-sparkplug PRIVATE ${Protobuf_INCLUDE_DIRS})
    target_compile_definitions(ipb-scoop-sparkplug PRIVATE IPB_HAS_PROTOBUF=1)
endif()

# Compiler options
if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU" OR CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
    target_compile_options(ipb-scoop-sparkplug PRIVATE
        -Wall -Wextra -Wpedantic
        -fPIC
    )
    if(CMAKE_BUILD_TYPE STREQUAL "Release")
        target_compile_options(ipb-scoop-sparkplug PRIVATE -O3)
        if(NOT APPLE)
            target_compile_options(ipb-scoop-sparkplug PRIVATE -march=native)
        endif()
    endif()
elseif(MSVC)
    target_compile_options(ipb-scoop-sparkplug PRIVATE /W4)
    if(CMAKE_BUILD_TYPE STREQUAL "Release")
        target_compile_options(ipb-scoop-sparkplug PRIVATE /O2)
    endif()
endif()

# Set library properties
set_target_properties(ipb-scoop-sparkplug PROPERTIES
    VERSION ${PROJECT_VERSION}
    SOVERSION 1
    OUTPUT_NAME ipb-scoop-sparkplug
)

# Install targets
install(TARGETS ipb-scoop-sparkplug
    LIBRARY DESTINATION lib
    ARCHIVE DESTINATION lib
)

install(DIRECTORY include/
    DESTINATION include
    FILES_MATCHING PATTERN "*.hpp"
)

# Alias for use in parent project
add_library(ipb::scoop-sparkplug ALIAS ipb-scoop-sparkplug)
