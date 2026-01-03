# IPB Tests
cmake_minimum_required(VERSION 3.20)

project(ipb-tests
    VERSION 1.0.0
    DESCRIPTION "IPB Test Suite"
    LANGUAGES CXX
)

# Find testing frameworks (GMock is included with GTest in modern versions)
# First try to find GTest on the system
find_package(GTest QUIET)

if(NOT GTest_FOUND)
    # GTest not found - use FetchContent to download it
    message(STATUS "GTest not found on system - downloading via FetchContent")
    include(FetchContent)

    FetchContent_Declare(
        googletest
        GIT_REPOSITORY https://github.com/google/googletest.git
        GIT_TAG v1.14.0
    )

    # Prevent GTest from overriding parent project's compiler/linker settings (Windows)
    set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
    # Don't install GTest when installing this project
    set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)

    FetchContent_MakeAvailable(googletest)

    # Create aliases to match the installed GTest target names
    if(NOT TARGET GTest::gtest)
        add_library(GTest::gtest ALIAS gtest)
    endif()
    if(NOT TARGET GTest::gtest_main)
        add_library(GTest::gtest_main ALIAS gtest_main)
    endif()
    if(NOT TARGET GTest::gmock)
        add_library(GTest::gmock ALIAS gmock)
    endif()
    if(NOT TARGET GTest::gmock_main)
        add_library(GTest::gmock_main ALIAS gmock_main)
    endif()

    message(STATUS "GTest fetched successfully")
else()
    message(STATUS "Using system GTest")
endif()

if(ENABLE_BENCHMARKS)
    find_package(benchmark REQUIRED)
endif()

# Include directories
include_directories(
    ${CMAKE_SOURCE_DIR}/core/common/include
    ${CMAKE_SOURCE_DIR}/core/components/include
    ${CMAKE_SOURCE_DIR}/core/router/include
    ${CMAKE_SOURCE_DIR}/apps/ipb-gate/include
)

# Unit tests
add_subdirectory(unit)

# Integration tests (if directory exists)
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/integration/CMakeLists.txt")
    add_subdirectory(integration)
endif()

# Performance tests
if(ENABLE_BENCHMARKS AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/performance/CMakeLists.txt")
    add_subdirectory(performance)
endif()

# System tests (if directory exists)
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/system/CMakeLists.txt")
    add_subdirectory(system)
endif()

# E2E tests (if directory exists and enabled)
# NOTE: Temporarily disabled due to Value template linkage issues
option(BUILD_E2E_TESTS "Build E2E tests" OFF)
if(BUILD_E2E_TESTS AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/e2e/CMakeLists.txt")
    add_subdirectory(e2e)
endif()

# Custom test targets
add_custom_target(test-unit
    COMMAND ${CMAKE_CTEST_COMMAND} --output-on-failure -L unit
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
    COMMENT "Running unit tests"
)

add_custom_target(test-all
    COMMAND ${CMAKE_CTEST_COMMAND} --output-on-failure
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
    COMMENT "Running all tests"
)

# Test coverage
if(ENABLE_COVERAGE)
    find_program(LCOV_PATH lcov)
    find_program(GENHTML_PATH genhtml)

    if(LCOV_PATH AND GENHTML_PATH)
        add_custom_target(test-coverage
            COMMAND ${CMAKE_CTEST_COMMAND} --output-on-failure
            COMMAND ${LCOV_PATH} --directory . --capture --output-file coverage.info
            COMMAND ${LCOV_PATH} --remove coverage.info '/usr/*' '*/tests/*' --output-file coverage.info
            COMMAND ${GENHTML_PATH} coverage.info --output-directory coverage_html
            WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
            COMMENT "Running tests with coverage analysis"
        )
    else()
        message(STATUS "lcov/genhtml not found, coverage target disabled")
    endif()
endif()

# Memory testing with Valgrind
if(ENABLE_VALGRIND)
    find_program(VALGRIND_PATH valgrind)
    if(VALGRIND_PATH)
        add_custom_target(test-valgrind
            COMMAND ${CMAKE_CTEST_COMMAND} --output-on-failure -T memcheck
            WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
            COMMENT "Running tests with Valgrind"
        )
    endif()
endif()
