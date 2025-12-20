# HTTP Transport Library
# Provides HTTP client functionality with multiple backend support:
# - libcurl (default, portable)
# - Boost.Beast (high-performance)

cmake_minimum_required(VERSION 3.16)

project(libipb-transport-http VERSION 1.0.0)

# C++ Standard
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

#=============================================================================
# Backend Options
#=============================================================================

option(IPB_HTTP_BACKEND_CURL "Enable libcurl HTTP backend (default)" ON)
option(IPB_HTTP_BACKEND_BEAST "Enable Boost.Beast HTTP backend" OFF)

#=============================================================================
# Find Dependencies
#=============================================================================

set(BACKENDS_AVAILABLE "")
set(BACKEND_SOURCES "")
set(BACKEND_LIBS "")
set(BACKEND_INCLUDES "")
set(BACKEND_DEFINES "")

# libcurl Backend
if(IPB_HTTP_BACKEND_CURL)
    find_package(CURL QUIET)
    if(NOT CURL_FOUND)
        find_library(CURL_LIB curl)
        find_path(CURL_INCLUDE curl/curl.h)
        if(CURL_LIB AND CURL_INCLUDE)
            set(CURL_FOUND TRUE)
            set(CURL_LIBRARIES ${CURL_LIB})
            set(CURL_INCLUDE_DIRS ${CURL_INCLUDE})
        endif()
    endif()

    if(CURL_FOUND)
        message(STATUS "libcurl HTTP backend: ENABLED")
        list(APPEND BACKENDS_AVAILABLE "curl")
        list(APPEND BACKEND_SOURCES src/backends/curl_backend.cpp)
        list(APPEND BACKEND_LIBS ${CURL_LIBRARIES})
        list(APPEND BACKEND_INCLUDES ${CURL_INCLUDE_DIRS})
        list(APPEND BACKEND_DEFINES IPB_HAS_CURL=1)
    else()
        message(STATUS "libcurl HTTP backend: NOT FOUND")
        message(STATUS "  Install with: sudo apt install libcurl4-openssl-dev (Ubuntu)")
        # Still compile stub
        list(APPEND BACKEND_SOURCES src/backends/curl_backend.cpp)
    endif()
else()
    # Compile stub even if not enabled
    list(APPEND BACKEND_SOURCES src/backends/curl_backend.cpp)
endif()

# Boost.Beast Backend
if(IPB_HTTP_BACKEND_BEAST)
    find_package(Boost QUIET COMPONENTS system)
    if(Boost_FOUND)
        message(STATUS "Boost.Beast HTTP backend: ENABLED")
        list(APPEND BACKENDS_AVAILABLE "beast")
        # Beast sources would be added here
        list(APPEND BACKEND_LIBS ${Boost_LIBRARIES})
        list(APPEND BACKEND_INCLUDES ${Boost_INCLUDE_DIRS})
        list(APPEND BACKEND_DEFINES IPB_HAS_BEAST=1)
    else()
        message(STATUS "Boost.Beast HTTP backend: NOT FOUND")
    endif()
endif()

# Check we have at least one backend
if(NOT BACKENDS_AVAILABLE)
    message(WARNING "No HTTP backends found. HTTP transport layer will not function.")
endif()

#=============================================================================
# Source Files
#=============================================================================

set(TRANSPORT_HTTP_SOURCES
    src/http_client.cpp
    src/backends/backend_factory.cpp
    ${BACKEND_SOURCES}
)

#=============================================================================
# Library Target
#=============================================================================

add_library(ipb-transport-http SHARED ${TRANSPORT_HTTP_SOURCES})

# Include directories
target_include_directories(ipb-transport-http
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
    PRIVATE
        ${BACKEND_INCLUDES}
)

# Link dependencies
target_link_libraries(ipb-transport-http
    PRIVATE
        ${BACKEND_LIBS}
)
if(NOT WIN32)
    target_link_libraries(ipb-transport-http PUBLIC pthread)
endif()

# Link libipb-security if available (for TLS support)
option(IPB_HTTP_USE_SECURITY "Use libipb-security for TLS" ON)
if(IPB_HTTP_USE_SECURITY AND TARGET ipb-security)
    target_link_libraries(ipb-transport-http PRIVATE ipb-security)
    target_include_directories(ipb-transport-http PRIVATE
        ${CMAKE_SOURCE_DIR}/core/security/include
    )
    target_compile_definitions(ipb-transport-http PRIVATE IPB_HAS_SECURITY=1)
    message(STATUS "HTTP transport: libipb-security integration ENABLED")
endif()

# Compile definitions
target_compile_definitions(ipb-transport-http
    PRIVATE
        ${BACKEND_DEFINES}
)

# Compiler options
if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU" OR CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
    target_compile_options(ipb-transport-http PRIVATE
        -Wall -Wextra -Wpedantic
        -fPIC
    )
    if(CMAKE_BUILD_TYPE STREQUAL "Release")
        target_compile_options(ipb-transport-http PRIVATE -O3)
        if(NOT APPLE)
            target_compile_options(ipb-transport-http PRIVATE -march=native)
        endif()
    endif()
elseif(MSVC)
    target_compile_options(ipb-transport-http PRIVATE /W4)
    if(CMAKE_BUILD_TYPE STREQUAL "Release")
        target_compile_options(ipb-transport-http PRIVATE /O2)
    endif()
endif()

#=============================================================================
# Library Properties
#=============================================================================

set_target_properties(ipb-transport-http PROPERTIES
    VERSION ${PROJECT_VERSION}
    SOVERSION 1
    OUTPUT_NAME ipb-transport-http
)

#=============================================================================
# Installation
#=============================================================================

install(TARGETS ipb-transport-http
    EXPORT ipb-transport-http-targets
    LIBRARY DESTINATION lib
    ARCHIVE DESTINATION lib
    RUNTIME DESTINATION bin
)

install(DIRECTORY include/
    DESTINATION include
    FILES_MATCHING PATTERN "*.hpp"
)

# Export targets
install(EXPORT ipb-transport-http-targets
    FILE ipb-transport-http-targets.cmake
    NAMESPACE ipb::
    DESTINATION lib/cmake/ipb-transport-http
)

#=============================================================================
# Package Configuration
#=============================================================================

include(CMakePackageConfigHelpers)

configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/ipb-transport-http-config.cmake.in"
    "${CMAKE_CURRENT_BINARY_DIR}/ipb-transport-http-config.cmake"
    @ONLY
)

write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/ipb-transport-http-config-version.cmake"
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion
)

install(FILES
    "${CMAKE_CURRENT_BINARY_DIR}/ipb-transport-http-config.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/ipb-transport-http-config-version.cmake"
    DESTINATION lib/cmake/ipb-transport-http
)

#=============================================================================
# Alias for Parent Project
#=============================================================================

add_library(ipb::transport-http ALIAS ipb-transport-http)

#=============================================================================
# Summary
#=============================================================================

message(STATUS "")
message(STATUS "HTTP Transport Configuration:")
message(STATUS "  Available backends: ${BACKENDS_AVAILABLE}")
message(STATUS "")
