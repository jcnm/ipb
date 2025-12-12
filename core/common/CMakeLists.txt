cmake_minimum_required(VERSION 3.20)

project(libipb-common
    VERSION 1.0.0
    DESCRIPTION "IPB Common Library - High-performance real-time data structures and interfaces"
    LANGUAGES CXX
)

# C++ Standard
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Build options
option(IPB_COMMON_BUILD_TESTS "Build tests for libipb-common" ON)
option(IPB_COMMON_BUILD_EXAMPLES "Build examples for libipb-common" ON)
option(IPB_COMMON_ENABLE_BENCHMARKS "Enable performance benchmarks" OFF)
option(IPB_COMMON_ENABLE_ASAN "Enable AddressSanitizer" OFF)
option(IPB_COMMON_ENABLE_TSAN "Enable ThreadSanitizer" OFF)
option(IPB_COMMON_ENABLE_UBSAN "Enable UndefinedBehaviorSanitizer" OFF)

# Performance options
option(IPB_COMMON_ENABLE_LTO "Enable Link Time Optimization" ON)
option(IPB_COMMON_ENABLE_NATIVE "Enable native CPU optimizations" ON)
option(IPB_COMMON_ENABLE_FAST_MATH "Enable fast math optimizations" OFF)

# Real-time options
option(IPB_COMMON_ENABLE_REALTIME "Enable real-time optimizations" ON)
option(IPB_COMMON_ENABLE_LOCK_FREE "Enable lock-free data structures" ON)
option(IPB_COMMON_ENABLE_ZERO_COPY "Enable zero-copy optimizations" ON)

# Platform detection
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    set(IPB_PLATFORM_LINUX TRUE)
elseif(CMAKE_SYSTEM_NAME STREQUAL "Windows")
    set(IPB_PLATFORM_WINDOWS TRUE)
elseif(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
    set(IPB_PLATFORM_MACOS TRUE)
endif()

# Compiler-specific options
if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    set(IPB_COMPILER_GCC TRUE)
elseif(CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
    set(IPB_COMPILER_CLANG TRUE)
elseif(CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")
    set(IPB_COMPILER_MSVC TRUE)
endif()

# Configure compile flags
set(IPB_COMMON_COMPILE_FLAGS)
set(IPB_COMMON_LINK_FLAGS)

# Base flags
if(IPB_COMPILER_GCC OR IPB_COMPILER_CLANG)
    list(APPEND IPB_COMMON_COMPILE_FLAGS
        -Wall -Wextra -Wpedantic
        -Wno-unused-parameter
        -fno-exceptions  # Real-time friendly
        -fno-rtti       # Reduce overhead
    )
    
    if(IPB_COMMON_ENABLE_REALTIME)
        list(APPEND IPB_COMMON_COMPILE_FLAGS
            -fno-stack-protector
            -fomit-frame-pointer
            -march=native
        )
    endif()
    
    if(IPB_COMMON_ENABLE_NATIVE)
        list(APPEND IPB_COMMON_COMPILE_FLAGS -march=native -mtune=native)
    endif()
    
    if(IPB_COMMON_ENABLE_FAST_MATH)
        list(APPEND IPB_COMMON_COMPILE_FLAGS -ffast-math)
    endif()
    
elseif(IPB_COMPILER_MSVC)
    list(APPEND IPB_COMMON_COMPILE_FLAGS
        /W4
        /permissive-
        /GR-  # Disable RTTI
    )
    
    if(IPB_COMMON_ENABLE_NATIVE)
        list(APPEND IPB_COMMON_COMPILE_FLAGS /arch:AVX2)
    endif()
endif()

# Debug/Release specific flags
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    if(IPB_COMPILER_GCC OR IPB_COMPILER_CLANG)
        list(APPEND IPB_COMMON_COMPILE_FLAGS -g3 -O0)
    elseif(IPB_COMPILER_MSVC)
        list(APPEND IPB_COMMON_COMPILE_FLAGS /Od /Zi)
    endif()
else()
    if(IPB_COMPILER_GCC OR IPB_COMPILER_CLANG)
        list(APPEND IPB_COMMON_COMPILE_FLAGS -O3 -DNDEBUG)
    elseif(IPB_COMPILER_MSVC)
        list(APPEND IPB_COMMON_COMPILE_FLAGS /O2 /DNDEBUG)
    endif()
endif()

# Sanitizers
if(IPB_COMMON_ENABLE_ASAN AND (IPB_COMPILER_GCC OR IPB_COMPILER_CLANG))
    list(APPEND IPB_COMMON_COMPILE_FLAGS -fsanitize=address -fno-omit-frame-pointer)
    list(APPEND IPB_COMMON_LINK_FLAGS -fsanitize=address)
endif()

if(IPB_COMMON_ENABLE_TSAN AND (IPB_COMPILER_GCC OR IPB_COMPILER_CLANG))
    list(APPEND IPB_COMMON_COMPILE_FLAGS -fsanitize=thread)
    list(APPEND IPB_COMMON_LINK_FLAGS -fsanitize=thread)
endif()

if(IPB_COMMON_ENABLE_UBSAN AND (IPB_COMPILER_GCC OR IPB_COMPILER_CLANG))
    list(APPEND IPB_COMMON_COMPILE_FLAGS -fsanitize=undefined)
    list(APPEND IPB_COMMON_LINK_FLAGS -fsanitize=undefined)
endif()

# LTO
if(IPB_COMMON_ENABLE_LTO AND CMAKE_BUILD_TYPE STREQUAL "Release")
    if(IPB_COMPILER_GCC OR IPB_COMPILER_CLANG)
        list(APPEND IPB_COMMON_COMPILE_FLAGS -flto)
        list(APPEND IPB_COMMON_LINK_FLAGS -flto)
    elseif(IPB_COMPILER_MSVC)
        list(APPEND IPB_COMMON_COMPILE_FLAGS /GL)
        list(APPEND IPB_COMMON_LINK_FLAGS /LTCG)
    endif()
endif()

# Find dependencies
find_package(Threads REQUIRED)

# Create main library
add_library(ipb-common STATIC
    src/data_point.cpp
    src/dataset.cpp
    src/endpoint.cpp
    src/interfaces.cpp
    src/rt_primitives.cpp
    src/platform.cpp
    src/error.cpp
    src/debug.cpp
    src/memory_pool.cpp
)

# Set target properties
set_target_properties(ipb-common PROPERTIES
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS OFF
    POSITION_INDEPENDENT_CODE ON
)

# Include directories
target_include_directories(ipb-common
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

# Compile options
target_compile_options(ipb-common PRIVATE ${IPB_COMMON_COMPILE_FLAGS})
target_link_options(ipb-common PRIVATE ${IPB_COMMON_LINK_FLAGS})

# Link libraries
target_link_libraries(ipb-common
    PUBLIC
        Threads::Threads
)

# Platform-specific libraries
if(IPB_PLATFORM_LINUX)
    target_link_libraries(ipb-common PRIVATE rt)
    target_compile_definitions(ipb-common PRIVATE IPB_PLATFORM_LINUX=1)
elseif(IPB_PLATFORM_WINDOWS)
    target_link_libraries(ipb-common PRIVATE winmm)
    target_compile_definitions(ipb-common PRIVATE IPB_PLATFORM_WINDOWS=1)
elseif(IPB_PLATFORM_MACOS)
    target_compile_definitions(ipb-common PRIVATE IPB_PLATFORM_MACOS=1)
endif()

# Feature definitions
if(IPB_COMMON_ENABLE_REALTIME)
    target_compile_definitions(ipb-common PUBLIC IPB_ENABLE_REALTIME=1)
endif()

if(IPB_COMMON_ENABLE_LOCK_FREE)
    target_compile_definitions(ipb-common PUBLIC IPB_ENABLE_LOCK_FREE=1)
endif()

if(IPB_COMMON_ENABLE_ZERO_COPY)
    target_compile_definitions(ipb-common PUBLIC IPB_ENABLE_ZERO_COPY=1)
endif()

# Create alias for consistent naming
add_library(ipb::common ALIAS ipb-common)

# Tests
if(IPB_COMMON_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

# Examples
if(IPB_COMMON_BUILD_EXAMPLES)
    add_subdirectory(examples)
endif()

# Installation
include(GNUInstallDirs)

# Install targets
install(TARGETS ipb-common
    EXPORT ipb-common-targets
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
install(EXPORT ipb-common-targets
    FILE ipb-common-targets.cmake
    NAMESPACE ipb::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/ipb-common
)

# Create config file
include(CMakePackageConfigHelpers)

configure_package_config_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/cmake/ipb-common-config.cmake.in
    ${CMAKE_CURRENT_BINARY_DIR}/ipb-common-config.cmake
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/ipb-common
)

write_basic_package_version_file(
    ${CMAKE_CURRENT_BINARY_DIR}/ipb-common-config-version.cmake
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion
)

install(FILES
    ${CMAKE_CURRENT_BINARY_DIR}/ipb-common-config.cmake
    ${CMAKE_CURRENT_BINARY_DIR}/ipb-common-config-version.cmake
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/ipb-common
)

# Create pkg-config file
configure_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/cmake/ipb-common.pc.in
    ${CMAKE_CURRENT_BINARY_DIR}/ipb-common.pc
    @ONLY
)

install(FILES
    ${CMAKE_CURRENT_BINARY_DIR}/ipb-common.pc
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/pkgconfig
)

# Summary
message(STATUS "")
message(STATUS "IPB Common Library Configuration Summary:")
message(STATUS "  Version: ${PROJECT_VERSION}")
message(STATUS "  Build type: ${CMAKE_BUILD_TYPE}")
message(STATUS "  C++ standard: ${CMAKE_CXX_STANDARD}")
message(STATUS "  Platform: ${CMAKE_SYSTEM_NAME}")
message(STATUS "  Compiler: ${CMAKE_CXX_COMPILER_ID}")
message(STATUS "")
message(STATUS "Features:")
message(STATUS "  Real-time optimizations: ${IPB_COMMON_ENABLE_REALTIME}")
message(STATUS "  Lock-free data structures: ${IPB_COMMON_ENABLE_LOCK_FREE}")
message(STATUS "  Zero-copy optimizations: ${IPB_COMMON_ENABLE_ZERO_COPY}")
message(STATUS "  Link Time Optimization: ${IPB_COMMON_ENABLE_LTO}")
message(STATUS "  Native CPU optimizations: ${IPB_COMMON_ENABLE_NATIVE}")
message(STATUS "  Fast math: ${IPB_COMMON_ENABLE_FAST_MATH}")
message(STATUS "")
message(STATUS "Build options:")
message(STATUS "  Build tests: ${IPB_COMMON_BUILD_TESTS}")
message(STATUS "  Build examples: ${IPB_COMMON_BUILD_EXAMPLES}")
message(STATUS "  Enable benchmarks: ${IPB_COMMON_ENABLE_BENCHMARKS}")
message(STATUS "")
message(STATUS "Sanitizers:")
message(STATUS "  AddressSanitizer: ${IPB_COMMON_ENABLE_ASAN}")
message(STATUS "  ThreadSanitizer: ${IPB_COMMON_ENABLE_TSAN}")
message(STATUS "  UndefinedBehaviorSanitizer: ${IPB_COMMON_ENABLE_UBSAN}")
message(STATUS "")

