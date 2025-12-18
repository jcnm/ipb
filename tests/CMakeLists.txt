# IPB Tests
cmake_minimum_required(VERSION 3.20)

project(ipb-tests
    VERSION 1.0.0
    DESCRIPTION "IPB Test Suite"
    LANGUAGES CXX
)

# Find testing frameworks (GMock is included with GTest in modern versions)
find_package(GTest REQUIRED)

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

# E2E tests (if directory exists)
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/e2e/CMakeLists.txt")
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
