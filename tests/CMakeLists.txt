# IPB Tests
cmake_minimum_required(VERSION 3.20)

project(ipb-tests
    VERSION 1.0.0
    DESCRIPTION "IPB Test Suite"
    LANGUAGES CXX
)

# Find testing frameworks
find_package(GTest REQUIRED)
find_package(GMock REQUIRED)

if(ENABLE_BENCHMARKS)
    find_package(benchmark REQUIRED)
endif()

# Include directories
include_directories(
    ${CMAKE_SOURCE_DIR}/core/common/include
    ${CMAKE_SOURCE_DIR}/core/router/include
    ${CMAKE_SOURCE_DIR}/apps/ipb-gate/include
)

# Test utilities library
add_library(ipb-test-utils STATIC
    utils/test_helpers.cpp
    utils/mock_components.cpp
    utils/performance_utils.cpp
    utils/data_generators.cpp
)

target_link_libraries(ipb-test-utils PUBLIC
    ipb-common
    GTest::gtest
    GMock::gmock
)

target_include_directories(ipb-test-utils PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/utils
)

# Unit tests
add_subdirectory(unit)

# Integration tests
add_subdirectory(integration)

# Performance tests
if(ENABLE_BENCHMARKS)
    add_subdirectory(performance)
endif()

# System tests
add_subdirectory(system)

# Test runner script
configure_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/scripts/run_tests.sh.in
    ${CMAKE_CURRENT_BINARY_DIR}/run_tests.sh
    @ONLY
)

# Custom test targets
add_custom_target(test-unit
    COMMAND ${CMAKE_CTEST_COMMAND} --output-on-failure -L unit
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
    COMMENT "Running unit tests"
)

add_custom_target(test-integration
    COMMAND ${CMAKE_CTEST_COMMAND} --output-on-failure -L integration
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
    COMMENT "Running integration tests"
)

add_custom_target(test-performance
    COMMAND ${CMAKE_CTEST_COMMAND} --output-on-failure -L performance
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
    COMMENT "Running performance tests"
)

add_custom_target(test-system
    COMMAND ${CMAKE_CTEST_COMMAND} --output-on-failure -L system
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
    COMMENT "Running system tests"
)

add_custom_target(test-all
    COMMAND ${CMAKE_CTEST_COMMAND} --output-on-failure
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
    COMMENT "Running all tests"
)

# Test coverage
if(ENABLE_COVERAGE)
    add_custom_target(test-coverage
        COMMAND ${CMAKE_CTEST_COMMAND} --output-on-failure
        COMMAND lcov --directory . --capture --output-file coverage.info
        COMMAND lcov --remove coverage.info '/usr/*' '*/tests/*' --output-file coverage.info
        COMMAND genhtml coverage.info --output-directory coverage_html
        WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
        COMMENT "Running tests with coverage analysis"
    )
endif()

# Memory testing with Valgrind
if(ENABLE_VALGRIND)
    add_custom_target(test-valgrind
        COMMAND ${CMAKE_CTEST_COMMAND} --output-on-failure -T memcheck
        WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
        COMMENT "Running tests with Valgrind"
    )
endif()

# Continuous testing
add_custom_target(test-watch
    COMMAND ${CMAKE_CURRENT_BINARY_DIR}/run_tests.sh --watch
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
    COMMENT "Running tests in watch mode"
)

