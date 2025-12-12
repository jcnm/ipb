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
    LABELS "unit;core;common"
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
    LABELS "unit;core;common"
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
    LABELS "unit;core;common"
    TIMEOUT 120
)

# ============================================================================
# Core/Components Tests - Coverage of scheduler, message_bus, rule_engine, etc.
# ============================================================================

# EDF Scheduler test
add_executable(test_scheduler test_scheduler.cpp)
target_link_libraries(test_scheduler PRIVATE
    ipb-core-components
    ipb-common
    GTest::gtest
    GTest::gtest_main
    Threads::Threads
)

add_test(NAME test_scheduler COMMAND test_scheduler)
set_tests_properties(test_scheduler PROPERTIES
    LABELS "unit;core;components"
    TIMEOUT 180
)

# Message Bus test
add_executable(test_message_bus test_message_bus.cpp)
target_link_libraries(test_message_bus PRIVATE
    ipb-core-components
    ipb-common
    GTest::gtest
    GTest::gtest_main
    Threads::Threads
)

add_test(NAME test_message_bus COMMAND test_message_bus)
set_tests_properties(test_message_bus PROPERTIES
    LABELS "unit;core;components"
    TIMEOUT 180
)

# Rule Engine test
add_executable(test_rule_engine test_rule_engine.cpp)
target_link_libraries(test_rule_engine PRIVATE
    ipb-core-components
    ipb-common
    GTest::gtest
    GTest::gtest_main
    Threads::Threads
)

add_test(NAME test_rule_engine COMMAND test_rule_engine)
set_tests_properties(test_rule_engine PROPERTIES
    LABELS "unit;core;components"
    TIMEOUT 180
)

# Sink Registry test
add_executable(test_sink_registry test_sink_registry.cpp)
target_link_libraries(test_sink_registry PRIVATE
    ipb-core-components
    ipb-common
    GTest::gtest
    GTest::gtest_main
    Threads::Threads
)

add_test(NAME test_sink_registry COMMAND test_sink_registry)
set_tests_properties(test_sink_registry PROPERTIES
    LABELS "unit;core;components"
    TIMEOUT 180
)

# ============================================================================
# Core/Router Tests - Coverage of router functionality
# ============================================================================

# Router test
add_executable(test_router test_router.cpp)
target_link_libraries(test_router PRIVATE
    ipb-router
    ipb-core-components
    ipb-common
    GTest::gtest
    GTest::gtest_main
    Threads::Threads
)

add_test(NAME test_router COMMAND test_router)
set_tests_properties(test_router PROPERTIES
    LABELS "unit;core;router"
    TIMEOUT 180
)

# ============================================================================
# Coverage Configuration
# ============================================================================

if(ENABLE_COVERAGE)
    # Core/Common tests
    target_compile_options(test_data_point PRIVATE --coverage)
    target_link_options(test_data_point PRIVATE --coverage)
    target_compile_options(test_error PRIVATE --coverage)
    target_link_options(test_error PRIVATE --coverage)
    target_compile_options(test_endpoint PRIVATE --coverage)
    target_link_options(test_endpoint PRIVATE --coverage)

    # Core/Components tests
    target_compile_options(test_scheduler PRIVATE --coverage)
    target_link_options(test_scheduler PRIVATE --coverage)
    target_compile_options(test_message_bus PRIVATE --coverage)
    target_link_options(test_message_bus PRIVATE --coverage)
    target_compile_options(test_rule_engine PRIVATE --coverage)
    target_link_options(test_rule_engine PRIVATE --coverage)
    target_compile_options(test_sink_registry PRIVATE --coverage)
    target_link_options(test_sink_registry PRIVATE --coverage)

    # Core/Router tests
    target_compile_options(test_router PRIVATE --coverage)
    target_link_options(test_router PRIVATE --coverage)
endif()

# ============================================================================
# Summary
# ============================================================================

message(STATUS "Configured unit tests:")
message(STATUS "  Core/Common:")
message(STATUS "    - test_data_point (Timestamp, Value, Quality, DataPoint, RawMessage)")
message(STATUS "    - test_error (ErrorCode, Error, Result, SourceLocation)")
message(STATUS "    - test_endpoint (EndPoint, ConnectionStats, rt::SPSCRingBuffer, rt::MemoryPool)")
message(STATUS "  Core/Components:")
message(STATUS "    - test_scheduler (EDFScheduler, ScheduledTask, TaskQueue)")
message(STATUS "    - test_message_bus (MessageBus, Message, Subscription, Channel)")
message(STATUS "    - test_rule_engine (RuleEngine, RoutingRule, PatternMatcher)")
message(STATUS "    - test_sink_registry (SinkRegistry, LoadBalancer, SinkInfo)")
message(STATUS "  Core/Router:")
message(STATUS "    - test_router (Router, RuleBuilder, RouterConfig)")
