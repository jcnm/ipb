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

# Helper function to create unit test executables
function(ipb_add_unit_test TEST_NAME TEST_FILE)
    add_executable(${TEST_NAME} ${TEST_FILE})
    target_link_libraries(${TEST_NAME} PRIVATE
        ipb-common
        GTest::gtest
        GTest::gtest_main
        Threads::Threads
    )

    # Add additional libraries if specified
    if(ARGC GREATER 2)
        target_link_libraries(${TEST_NAME} PRIVATE ${ARGN})
    endif()

    # Register test with CTest
    add_test(NAME ${TEST_NAME} COMMAND ${TEST_NAME})
    set_tests_properties(${TEST_NAME} PROPERTIES
        LABELS "unit"
        TIMEOUT 60
    )
endfunction()

# ============================================================================
# Core/Common Tests
# ============================================================================

# Timestamp tests
ipb_add_unit_test(test_timestamp test_timestamp.cpp)

# Value tests
ipb_add_unit_test(test_value test_value.cpp)

# DataPoint tests (new API)
ipb_add_unit_test(test_data_point_new test_data_point_new.cpp)

# Error handling tests
ipb_add_unit_test(test_error test_error.cpp)

# EndPoint and RT primitives tests
ipb_add_unit_test(test_endpoint test_endpoint.cpp)

# ============================================================================
# Core/Components Tests
# ============================================================================

# Check if ipb-components library exists
if(TARGET ipb-components)
    # MessageBus tests
    add_executable(test_message_bus test_message_bus.cpp)
    target_link_libraries(test_message_bus PRIVATE
        ipb-common
        ipb-components
        GTest::gtest
        GTest::gtest_main
        Threads::Threads
    )
    add_test(NAME test_message_bus COMMAND test_message_bus)
    set_tests_properties(test_message_bus PROPERTIES
        LABELS "unit;components"
        TIMEOUT 120
    )

    # Scheduler tests
    add_executable(test_scheduler test_scheduler.cpp)
    target_link_libraries(test_scheduler PRIVATE
        ipb-common
        ipb-components
        GTest::gtest
        GTest::gtest_main
        Threads::Threads
    )
    add_test(NAME test_scheduler COMMAND test_scheduler)
    set_tests_properties(test_scheduler PROPERTIES
        LABELS "unit;components;scheduler"
        TIMEOUT 120
    )
else()
    message(STATUS "ipb-components not available, skipping component tests")
endif()

# ============================================================================
# Legacy Tests (may need API updates)
# ============================================================================

# Original data_point test - may use older API
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/test_data_point.cpp")
    ipb_add_unit_test(test_data_point_legacy test_data_point.cpp)
    set_tests_properties(test_data_point_legacy PROPERTIES
        LABELS "unit;legacy"
    )
endif()

# ============================================================================
# Test Categories for filtering
# ============================================================================

# Performance-labeled tests run separately
set_tests_properties(test_timestamp test_value test_endpoint PROPERTIES
    LABELS "unit;performance"
)

# ============================================================================
# Coverage Configuration
# ============================================================================

if(ENABLE_COVERAGE)
    target_compile_options(test_timestamp PRIVATE --coverage)
    target_link_options(test_timestamp PRIVATE --coverage)

    target_compile_options(test_value PRIVATE --coverage)
    target_link_options(test_value PRIVATE --coverage)

    target_compile_options(test_data_point_new PRIVATE --coverage)
    target_link_options(test_data_point_new PRIVATE --coverage)

    target_compile_options(test_error PRIVATE --coverage)
    target_link_options(test_error PRIVATE --coverage)

    target_compile_options(test_endpoint PRIVATE --coverage)
    target_link_options(test_endpoint PRIVATE --coverage)
endif()

# ============================================================================
# Summary
# ============================================================================

message(STATUS "Configured unit tests:")
message(STATUS "  - test_timestamp")
message(STATUS "  - test_value")
message(STATUS "  - test_data_point_new")
message(STATUS "  - test_error")
message(STATUS "  - test_endpoint")
if(TARGET ipb-components)
    message(STATUS "  - test_message_bus")
    message(STATUS "  - test_scheduler")
endif()
