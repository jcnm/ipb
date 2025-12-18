# Console Scoop Tests
cmake_minimum_required(VERSION 3.20)

# Include the IPB plugin test module
include(${CMAKE_SOURCE_DIR}/cmake/modules/IPBPluginTest.cmake)

# Find required packages
find_package(GTest REQUIRED)
find_package(Threads REQUIRED)

# Create test executable
add_executable(ipb-scoop-console-tests
    test_console_scoop.cpp
)

# Set target properties
set_target_properties(ipb-scoop-console-tests PROPERTIES
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS OFF
)

# Include directories
target_include_directories(ipb-scoop-console-tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../include
    ${CMAKE_SOURCE_DIR}/core/common/include
    ${CMAKE_SOURCE_DIR}/tests/framework
)

# Link libraries
target_link_libraries(ipb-scoop-console-tests PRIVATE
    ipb-scoop-console
    ipb-common
    GTest::gtest
    GTest::gtest_main
    Threads::Threads
)

# Register tests with CTest
add_test(NAME ipb-scoop-console-tests COMMAND ipb-scoop-console-tests)
set_tests_properties(ipb-scoop-console-tests PROPERTIES
    LABELS "plugin;scoop;console"
    TIMEOUT 300
)

# GTest discovery
include(GoogleTest)
gtest_discover_tests(ipb-scoop-console-tests
    PROPERTIES
        LABELS "plugin;scoop;console"
        TIMEOUT 60
    DISCOVERY_MODE PRE_TEST
)

# Coverage support
if(ENABLE_COVERAGE)
    target_compile_options(ipb-scoop-console-tests PRIVATE --coverage)
    target_link_options(ipb-scoop-console-tests PRIVATE --coverage)
endif()

# Post-build test execution (optional)
if(IPB_RUN_TESTS_ON_BUILD)
    add_custom_command(TARGET ipb-scoop-console POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E echo "Running console scoop tests..."
        COMMAND $<TARGET_FILE:ipb-scoop-console-tests> || ${CMAKE_COMMAND} -E echo "Tests completed"
        COMMENT "Running ipb-scoop-console-tests post-build"
        VERBATIM
    )
endif()

message(STATUS "Console Scoop: Tests configured")
