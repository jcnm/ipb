cmake_minimum_required(VERSION 3.20)

project(libipb-transform
    VERSION 1.0.0
    DESCRIPTION "IPB Transform Library - Bijective data transformation pipeline"
    LANGUAGES CXX
)

# C++ Standard
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Build options
option(IPB_TRANSFORM_BUILD_TESTS "Build tests for libipb-transform" ON)
option(IPB_TRANSFORM_BUILD_EXAMPLES "Build examples for libipb-transform" OFF)
option(IPB_TRANSFORM_ENABLE_BENCHMARKS "Enable performance benchmarks" OFF)

# Compression library options
option(IPB_TRANSFORM_USE_ZSTD "Enable ZSTD compression" ON)
option(IPB_TRANSFORM_USE_LZ4 "Enable LZ4 compression" ON)
option(IPB_TRANSFORM_USE_SNAPPY "Enable Snappy compression" OFF)
option(IPB_TRANSFORM_USE_ZLIB "Enable ZLIB/GZIP compression" ON)

# Encryption library options
option(IPB_TRANSFORM_USE_OPENSSL "Enable OpenSSL encryption" ON)

# Integrity library options
option(IPB_TRANSFORM_USE_XXHASH "Enable xxHash for fast hashing" ON)

# Platform detection
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    set(IPB_PLATFORM_LINUX TRUE)
elseif(CMAKE_SYSTEM_NAME STREQUAL "Windows")
    set(IPB_PLATFORM_WINDOWS TRUE)
elseif(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
    set(IPB_PLATFORM_MACOS TRUE)
endif()

# Compiler detection
if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    set(IPB_COMPILER_GCC TRUE)
elseif(CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
    set(IPB_COMPILER_CLANG TRUE)
elseif(CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")
    set(IPB_COMPILER_MSVC TRUE)
endif()

# Configure compile flags
set(IPB_TRANSFORM_COMPILE_FLAGS)
set(IPB_TRANSFORM_LINK_FLAGS)

if(IPB_COMPILER_GCC OR IPB_COMPILER_CLANG)
    list(APPEND IPB_TRANSFORM_COMPILE_FLAGS
        -Wall -Wextra -Wpedantic
        -Wno-unused-parameter
    )
elseif(IPB_COMPILER_MSVC)
    list(APPEND IPB_TRANSFORM_COMPILE_FLAGS
        /W4
        /permissive-
    )
endif()

# Find dependencies
find_package(Threads REQUIRED)

# Find optional compression libraries
if(IPB_TRANSFORM_USE_ZSTD)
    find_package(PkgConfig QUIET)
    if(PkgConfig_FOUND)
        pkg_check_modules(ZSTD QUIET libzstd)
    endif()
    if(NOT ZSTD_FOUND)
        find_path(ZSTD_INCLUDE_DIR zstd.h)
        find_library(ZSTD_LIBRARY NAMES zstd)
        if(ZSTD_INCLUDE_DIR AND ZSTD_LIBRARY)
            set(ZSTD_FOUND TRUE)
        endif()
    endif()
    if(ZSTD_FOUND)
        message(STATUS "ZSTD compression: ENABLED")
    else()
        message(STATUS "ZSTD compression: DISABLED (library not found)")
    endif()
endif()

if(IPB_TRANSFORM_USE_LZ4)
    find_package(PkgConfig QUIET)
    if(PkgConfig_FOUND)
        pkg_check_modules(LZ4 QUIET liblz4)
    endif()
    if(NOT LZ4_FOUND)
        find_path(LZ4_INCLUDE_DIR lz4.h)
        find_library(LZ4_LIBRARY NAMES lz4)
        if(LZ4_INCLUDE_DIR AND LZ4_LIBRARY)
            set(LZ4_FOUND TRUE)
        endif()
    endif()
    if(LZ4_FOUND)
        message(STATUS "LZ4 compression: ENABLED")
    else()
        message(STATUS "LZ4 compression: DISABLED (library not found)")
    endif()
endif()

if(IPB_TRANSFORM_USE_SNAPPY)
    find_package(Snappy QUIET)
    if(NOT Snappy_FOUND)
        find_path(SNAPPY_INCLUDE_DIR snappy.h)
        find_library(SNAPPY_LIBRARY NAMES snappy)
        if(SNAPPY_INCLUDE_DIR AND SNAPPY_LIBRARY)
            set(SNAPPY_FOUND TRUE)
        endif()
    endif()
    if(SNAPPY_FOUND)
        message(STATUS "Snappy compression: ENABLED")
    else()
        message(STATUS "Snappy compression: DISABLED (library not found)")
    endif()
endif()

if(IPB_TRANSFORM_USE_ZLIB)
    find_package(ZLIB QUIET)
    if(ZLIB_FOUND)
        message(STATUS "ZLIB/GZIP compression: ENABLED")
    else()
        message(STATUS "ZLIB/GZIP compression: DISABLED (library not found)")
    endif()
endif()

# Find xxHash for fast hashing
if(IPB_TRANSFORM_USE_XXHASH)
    find_package(PkgConfig QUIET)
    if(PkgConfig_FOUND)
        pkg_check_modules(XXHASH QUIET libxxhash)
    endif()
    if(NOT XXHASH_FOUND)
        find_path(XXHASH_INCLUDE_DIR xxhash.h)
        find_library(XXHASH_LIBRARY NAMES xxhash)
        if(XXHASH_INCLUDE_DIR AND XXHASH_LIBRARY)
            set(XXHASH_FOUND TRUE)
        endif()
    endif()
    if(XXHASH_FOUND)
        message(STATUS "xxHash hashing: ENABLED")
    else()
        message(STATUS "xxHash hashing: DISABLED (library not found)")
    endif()
endif()

# Find OpenSSL for encryption
if(IPB_TRANSFORM_USE_OPENSSL)
    find_package(OpenSSL QUIET)
    if(OpenSSL_FOUND)
        message(STATUS "OpenSSL encryption: ENABLED (${OPENSSL_VERSION})")
    else()
        message(STATUS "OpenSSL encryption: DISABLED (library not found)")
    endif()
endif()

# Create main library
add_library(ipb-transform STATIC
    src/compression.cpp
    src/encryption.cpp
    src/encoding.cpp
    src/integrity.cpp
)

# Set target properties
set_target_properties(ipb-transform PROPERTIES
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS OFF
    POSITION_INDEPENDENT_CODE ON
)

# Include directories
target_include_directories(ipb-transform
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

# Link to ipb-common
target_link_libraries(ipb-transform
    PUBLIC
        ipb-common
        Threads::Threads
)

# Compile options
target_compile_options(ipb-transform PRIVATE ${IPB_TRANSFORM_COMPILE_FLAGS})
target_link_options(ipb-transform PRIVATE ${IPB_TRANSFORM_LINK_FLAGS})

# Link compression libraries
if(ZSTD_FOUND)
    if(ZSTD_INCLUDE_DIRS)
        target_include_directories(ipb-transform PRIVATE ${ZSTD_INCLUDE_DIRS})
    elseif(ZSTD_INCLUDE_DIR)
        target_include_directories(ipb-transform PRIVATE ${ZSTD_INCLUDE_DIR})
    endif()
    if(ZSTD_LIBRARIES)
        target_link_libraries(ipb-transform PRIVATE ${ZSTD_LIBRARIES})
    elseif(ZSTD_LIBRARY)
        target_link_libraries(ipb-transform PRIVATE ${ZSTD_LIBRARY})
    endif()
    target_compile_definitions(ipb-transform PRIVATE IPB_HAS_ZSTD=1)
endif()

if(LZ4_FOUND)
    if(LZ4_INCLUDE_DIRS)
        target_include_directories(ipb-transform PRIVATE ${LZ4_INCLUDE_DIRS})
    elseif(LZ4_INCLUDE_DIR)
        target_include_directories(ipb-transform PRIVATE ${LZ4_INCLUDE_DIR})
    endif()
    if(LZ4_LIBRARIES)
        target_link_libraries(ipb-transform PRIVATE ${LZ4_LIBRARIES})
    elseif(LZ4_LIBRARY)
        target_link_libraries(ipb-transform PRIVATE ${LZ4_LIBRARY})
    endif()
    target_compile_definitions(ipb-transform PRIVATE IPB_HAS_LZ4=1)
endif()

if(SNAPPY_FOUND)
    if(SNAPPY_INCLUDE_DIR)
        target_include_directories(ipb-transform PRIVATE ${SNAPPY_INCLUDE_DIR})
    endif()
    if(SNAPPY_LIBRARY)
        target_link_libraries(ipb-transform PRIVATE ${SNAPPY_LIBRARY})
    elseif(TARGET Snappy::snappy)
        target_link_libraries(ipb-transform PRIVATE Snappy::snappy)
    endif()
    target_compile_definitions(ipb-transform PRIVATE IPB_HAS_SNAPPY=1)
endif()

if(ZLIB_FOUND)
    target_link_libraries(ipb-transform PRIVATE ZLIB::ZLIB)
    target_compile_definitions(ipb-transform PRIVATE IPB_HAS_ZLIB=1)
endif()

# Link OpenSSL for encryption
if(OpenSSL_FOUND)
    target_link_libraries(ipb-transform PRIVATE OpenSSL::SSL OpenSSL::Crypto)
    target_compile_definitions(ipb-transform PRIVATE IPB_HAS_OPENSSL=1)
endif()

# Link xxHash for fast hashing
if(XXHASH_FOUND)
    if(XXHASH_INCLUDE_DIRS)
        target_include_directories(ipb-transform PRIVATE ${XXHASH_INCLUDE_DIRS})
    elseif(XXHASH_INCLUDE_DIR)
        target_include_directories(ipb-transform PRIVATE ${XXHASH_INCLUDE_DIR})
    endif()
    if(XXHASH_LIBRARIES)
        target_link_libraries(ipb-transform PRIVATE ${XXHASH_LIBRARIES})
    elseif(XXHASH_LIBRARY)
        target_link_libraries(ipb-transform PRIVATE ${XXHASH_LIBRARY})
    endif()
    target_compile_definitions(ipb-transform PRIVATE IPB_HAS_XXHASH=1)
endif()

# Create alias for consistent naming
add_library(ipb::transform ALIAS ipb-transform)

# Tests
if(IPB_TRANSFORM_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

# Examples
if(IPB_TRANSFORM_BUILD_EXAMPLES)
    add_subdirectory(examples)
endif()

# Benchmarks
if(IPB_TRANSFORM_ENABLE_BENCHMARKS)
    add_subdirectory(benchmarks)
endif()

# Installation
include(GNUInstallDirs)

# Install targets
install(TARGETS ipb-transform
    EXPORT ipb-transform-targets
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)

# Install headers
install(DIRECTORY include/
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
    FILES_MATCHING PATTERN "*.hpp"
)

# Install export targets
install(EXPORT ipb-transform-targets
    FILE ipb-transform-targets.cmake
    NAMESPACE ipb::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/ipb-transform
)

# Summary
message(STATUS "")
message(STATUS "IPB Transform Library Configuration Summary:")
message(STATUS "  Version: ${PROJECT_VERSION}")
message(STATUS "  Build type: ${CMAKE_BUILD_TYPE}")
message(STATUS "  C++ standard: ${CMAKE_CXX_STANDARD}")
message(STATUS "")
message(STATUS "Compression backends:")
message(STATUS "  ZSTD: ${ZSTD_FOUND}")
message(STATUS "  LZ4: ${LZ4_FOUND}")
message(STATUS "  Snappy: ${SNAPPY_FOUND}")
message(STATUS "  ZLIB: ${ZLIB_FOUND}")
message(STATUS "")
message(STATUS "Encryption backends:")
message(STATUS "  OpenSSL: ${OpenSSL_FOUND}")
message(STATUS "")
message(STATUS "Integrity backends:")
message(STATUS "  ZLIB CRC32: ${ZLIB_FOUND}")
message(STATUS "  xxHash: ${XXHASH_FOUND}")
message(STATUS "")
message(STATUS "Build options:")
message(STATUS "  Build tests: ${IPB_TRANSFORM_BUILD_TESTS}")
message(STATUS "  Build examples: ${IPB_TRANSFORM_BUILD_EXAMPLES}")
message(STATUS "  Enable benchmarks: ${IPB_TRANSFORM_ENABLE_BENCHMARKS}")
message(STATUS "")
