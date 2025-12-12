# IPB Unit Tests
cmake_minimum_required(VERSION 3.20)

# Find required packages
find_package(GTest REQUIRED)
find_package(Threads REQUIRED)

# Include directories for test files
include_directories(
    ${CMAKE_SOURCE_DIR}/core/common/include
    ${CMAKE_SOURCE_DIR}/core/components/include
    ${CMAKE_SOURCE_DIR}/core/router/include
)

# ============================================================================
# Core/Common Tests - Complete coverage of core types
# ============================================================================

# DataPoint test (covers Timestamp, Value, Quality, DataPoint, RawMessage)
add_executable(test_data_point test_data_point.cpp)
target_link_libraries(test_data_point PRIVATE
    ipb-common
    GTest::gtest
    GTest::gtest_main
    Threads::Threads
)

add_test(NAME test_data_point COMMAND test_data_point)
set_tests_properties(test_data_point PROPERTIES
    LABELS "unit;core"
    TIMEOUT 120
)

# Error handling test (covers ErrorCode, Error, Result, SourceLocation)
add_executable(test_error test_error.cpp)
target_link_libraries(test_error PRIVATE
    ipb-common
    GTest::gtest
    GTest::gtest_main
    Threads::Threads
)

add_test(NAME test_error COMMAND test_error)
set_tests_properties(test_error PROPERTIES
    LABELS "unit;core"
    TIMEOUT 120
)

# EndPoint and RT primitives test (covers EndPoint, ConnectionStats, rt::*)
add_executable(test_endpoint test_endpoint.cpp)
target_link_libraries(test_endpoint PRIVATE
    ipb-common
    GTest::gtest
    GTest::gtest_main
    Threads::Threads
)

add_test(NAME test_endpoint COMMAND test_endpoint)
set_tests_properties(test_endpoint PROPERTIES
    LABELS "unit;core"
    TIMEOUT 120
)

# ============================================================================
# Coverage Configuration
# ============================================================================

if(ENABLE_COVERAGE)
    target_compile_options(test_data_point PRIVATE --coverage)
    target_link_options(test_data_point PRIVATE --coverage)
    target_compile_options(test_error PRIVATE --coverage)
    target_link_options(test_error PRIVATE --coverage)
    target_compile_options(test_endpoint PRIVATE --coverage)
    target_link_options(test_endpoint PRIVATE --coverage)
endif()

# ============================================================================
# Summary
# ============================================================================

message(STATUS "Configured unit tests:")
message(STATUS "  - test_data_point (Timestamp, Value, Quality, DataPoint, RawMessage)")
message(STATUS "  - test_error (ErrorCode, Error, Result, SourceLocation)")
message(STATUS "  - test_endpoint (EndPoint, ConnectionStats, rt::SPSCRingBuffer, rt::MemoryPool)")
