# Console Sink Tests
cmake_minimum_required(VERSION 3.20)

# Include the IPB plugin test module
include(${CMAKE_SOURCE_DIR}/cmake/modules/IPBPluginTest.cmake)

# Find required packages
find_package(GTest REQUIRED)
find_package(Threads REQUIRED)

# Create test executable
add_executable(ipb-sink-console-tests
    test_console_sink.cpp
)

# Set target properties
set_target_properties(ipb-sink-console-tests PROPERTIES
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS OFF
)

# Include directories
target_include_directories(ipb-sink-console-tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../include
    ${CMAKE_SOURCE_DIR}/core/common/include
    ${CMAKE_SOURCE_DIR}/tests/framework
)

# Link libraries
target_link_libraries(ipb-sink-console-tests PRIVATE
    ipb-sink-console
    ipb-common
    GTest::gtest
    GTest::gtest_main
    Threads::Threads
)

# Register tests with CTest
add_test(NAME ipb-sink-console-tests COMMAND ipb-sink-console-tests)
set_tests_properties(ipb-sink-console-tests PROPERTIES
    LABELS "plugin;sink;console"
    TIMEOUT 300
)

# GTest discovery
include(GoogleTest)
gtest_discover_tests(ipb-sink-console-tests
    PROPERTIES
        LABELS "plugin;sink;console"
        TIMEOUT 60
    DISCOVERY_MODE PRE_TEST
)

# Coverage support
if(ENABLE_COVERAGE)
    target_compile_options(ipb-sink-console-tests PRIVATE --coverage)
    target_link_options(ipb-sink-console-tests PRIVATE --coverage)
endif()

# Post-build test execution (optional)
if(IPB_RUN_TESTS_ON_BUILD)
    add_custom_command(TARGET ipb-sink-console POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E echo "Running console sink tests..."
        COMMAND $<TARGET_FILE:ipb-sink-console-tests> || ${CMAKE_COMMAND} -E echo "Tests completed"
        COMMENT "Running ipb-sink-console-tests post-build"
        VERBATIM
    )
endif()

message(STATUS "Console Sink: Tests configured")
