# IPBPluginTest.cmake
#
# CMake module for automatic test discovery and execution of IPB plugins (sinks/scoops)
#
# This module provides:
# - Automatic test discovery for 3rd party sinks/scoops
# - Post-build test execution hooks
# - Test registration without source code modification
#
# Usage for 3rd party plugins:
#   include(IPBPluginTest)
#   ipb_plugin_enable_tests(TARGET_NAME)
#
# This will:
# 1. Auto-discover test files matching *_test.cpp or test_*.cpp in tests/ directory
# 2. Create test executable linked with the plugin
# 3. Register tests with CTest
# 4. Optionally run tests after build (if IPB_RUN_TESTS_ON_BUILD is ON)

include(CMakeParseArguments)

# Option to run tests automatically after build
option(IPB_RUN_TESTS_ON_BUILD "Run plugin tests automatically after successful build" OFF)

# Option to fail build on test failure
option(IPB_FAIL_BUILD_ON_TEST_FAILURE "Fail the build if plugin tests fail" OFF)

#
# ipb_plugin_enable_tests
#
# Enables automatic test discovery and execution for a plugin
#
# Arguments:
#   TARGET_NAME - The name of the plugin target (required)
#
# Optional arguments:
#   TEST_DIR - Directory containing tests (default: ${CMAKE_CURRENT_SOURCE_DIR}/tests)
#   EXTRA_LIBS - Additional libraries to link with tests
#   EXTRA_INCLUDES - Additional include directories
#   RUN_AFTER_BUILD - Override global IPB_RUN_TESTS_ON_BUILD for this target
#
function(ipb_plugin_enable_tests TARGET_NAME)
    cmake_parse_arguments(PLUGIN_TEST
        "RUN_AFTER_BUILD"
        "TEST_DIR"
        "EXTRA_LIBS;EXTRA_INCLUDES"
        ${ARGN}
    )

    # Set defaults
    if(NOT PLUGIN_TEST_TEST_DIR)
        set(PLUGIN_TEST_TEST_DIR "${CMAKE_CURRENT_SOURCE_DIR}/tests")
    endif()

    # Check if test directory exists
    if(NOT EXISTS "${PLUGIN_TEST_TEST_DIR}")
        message(STATUS "IPBPluginTest: No tests directory found for ${TARGET_NAME} at ${PLUGIN_TEST_TEST_DIR}")
        return()
    endif()

    # Auto-discover test files
    file(GLOB TEST_SOURCES
        "${PLUGIN_TEST_TEST_DIR}/*_test.cpp"
        "${PLUGIN_TEST_TEST_DIR}/test_*.cpp"
    )

    if(NOT TEST_SOURCES)
        message(STATUS "IPBPluginTest: No test files found for ${TARGET_NAME}")
        return()
    endif()

    # Create test executable name
    set(TEST_TARGET "${TARGET_NAME}-tests")

    # Find GTest
    find_package(GTest REQUIRED)

    # Create test executable
    add_executable(${TEST_TARGET} ${TEST_SOURCES})

    # Set C++ standard
    set_target_properties(${TEST_TARGET} PROPERTIES
        CXX_STANDARD 20
        CXX_STANDARD_REQUIRED ON
        CXX_EXTENSIONS OFF
    )

    # Include directories
    target_include_directories(${TEST_TARGET} PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${CMAKE_SOURCE_DIR}/core/common/include
        ${CMAKE_SOURCE_DIR}/tests/framework
        ${PLUGIN_TEST_EXTRA_INCLUDES}
    )

    # Link libraries
    target_link_libraries(${TEST_TARGET} PRIVATE
        ${TARGET_NAME}
        ipb-common
        GTest::gtest
        GTest::gtest_main
        Threads::Threads
        ${PLUGIN_TEST_EXTRA_LIBS}
    )

    # Register with CTest
    add_test(NAME ${TEST_TARGET} COMMAND ${TEST_TARGET})
    set_tests_properties(${TEST_TARGET} PROPERTIES
        LABELS "plugin;${TARGET_NAME}"
        TIMEOUT 300
    )

    # GTest discovery for detailed test reporting
    include(GoogleTest)
    gtest_discover_tests(${TEST_TARGET}
        PROPERTIES
            LABELS "plugin;${TARGET_NAME}"
            TIMEOUT 60
        DISCOVERY_MODE PRE_TEST
    )

    # Post-build test execution
    set(RUN_TESTS_NOW ${IPB_RUN_TESTS_ON_BUILD})
    if(PLUGIN_TEST_RUN_AFTER_BUILD)
        set(RUN_TESTS_NOW ON)
    endif()

    if(RUN_TESTS_NOW)
        ipb_plugin_add_post_build_test(${TARGET_NAME} ${TEST_TARGET})
    endif()

    # Coverage support
    if(ENABLE_COVERAGE)
        target_compile_options(${TEST_TARGET} PRIVATE --coverage)
        target_link_options(${TEST_TARGET} PRIVATE --coverage)
    endif()

    message(STATUS "IPBPluginTest: Configured tests for ${TARGET_NAME} (${TEST_TARGET})")

endfunction()

#
# ipb_plugin_add_post_build_test
#
# Adds a post-build command to run tests after the plugin is built
#
function(ipb_plugin_add_post_build_test TARGET_NAME TEST_TARGET)
    if(IPB_FAIL_BUILD_ON_TEST_FAILURE)
        # Run tests and fail build if they fail
        add_custom_command(TARGET ${TARGET_NAME} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E echo "Running tests for ${TARGET_NAME}..."
            COMMAND $<TARGET_FILE:${TEST_TARGET}>
            COMMENT "Running ${TEST_TARGET} post-build tests"
            VERBATIM
        )
    else()
        # Run tests but don't fail build
        add_custom_command(TARGET ${TARGET_NAME} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E echo "Running tests for ${TARGET_NAME}..."
            COMMAND $<TARGET_FILE:${TEST_TARGET}> || ${CMAKE_COMMAND} -E echo "Tests failed (non-fatal)"
            COMMENT "Running ${TEST_TARGET} post-build tests (non-fatal)"
            VERBATIM
        )
    endif()
endfunction()

#
# ipb_plugin_add_benchmark
#
# Adds benchmark tests for a plugin (optional Google Benchmark support)
#
function(ipb_plugin_add_benchmark TARGET_NAME)
    cmake_parse_arguments(PLUGIN_BENCH
        ""
        "BENCHMARK_DIR"
        "EXTRA_LIBS;EXTRA_INCLUDES"
        ${ARGN}
    )

    if(NOT PLUGIN_BENCH_BENCHMARK_DIR)
        set(PLUGIN_BENCH_BENCHMARK_DIR "${CMAKE_CURRENT_SOURCE_DIR}/benchmarks")
    endif()

    if(NOT EXISTS "${PLUGIN_BENCH_BENCHMARK_DIR}")
        return()
    endif()

    find_package(benchmark QUIET)
    if(NOT benchmark_FOUND)
        message(STATUS "IPBPluginTest: Google Benchmark not found, skipping benchmarks for ${TARGET_NAME}")
        return()
    endif()

    file(GLOB BENCH_SOURCES
        "${PLUGIN_BENCH_BENCHMARK_DIR}/*_bench.cpp"
        "${PLUGIN_BENCH_BENCHMARK_DIR}/bench_*.cpp"
    )

    if(NOT BENCH_SOURCES)
        return()
    endif()

    set(BENCH_TARGET "${TARGET_NAME}-bench")

    add_executable(${BENCH_TARGET} ${BENCH_SOURCES})

    set_target_properties(${BENCH_TARGET} PROPERTIES
        CXX_STANDARD 20
        CXX_STANDARD_REQUIRED ON
    )

    target_include_directories(${BENCH_TARGET} PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${CMAKE_SOURCE_DIR}/core/common/include
        ${PLUGIN_BENCH_EXTRA_INCLUDES}
    )

    target_link_libraries(${BENCH_TARGET} PRIVATE
        ${TARGET_NAME}
        ipb-common
        benchmark::benchmark
        Threads::Threads
        ${PLUGIN_BENCH_EXTRA_LIBS}
    )

    message(STATUS "IPBPluginTest: Configured benchmarks for ${TARGET_NAME} (${BENCH_TARGET})")

endfunction()

#
# ipb_discover_plugin_tests
#
# Discovers and configures all plugin tests in a directory structure
# Call this from the root CMakeLists.txt to auto-discover all sinks/scoops tests
#
function(ipb_discover_plugin_tests ROOT_DIR)
    if(NOT EXISTS "${ROOT_DIR}")
        return()
    endif()

    # Find all subdirectories with CMakeLists.txt
    file(GLOB PLUGIN_DIRS "${ROOT_DIR}/*")

    foreach(PLUGIN_DIR ${PLUGIN_DIRS})
        if(IS_DIRECTORY "${PLUGIN_DIR}")
            get_filename_component(PLUGIN_NAME ${PLUGIN_DIR} NAME)

            # Check if plugin has tests
            if(EXISTS "${PLUGIN_DIR}/tests")
                # Check if target exists
                if(TARGET "ipb-sink-${PLUGIN_NAME}" OR TARGET "ipb-scoop-${PLUGIN_NAME}")
                    if(TARGET "ipb-sink-${PLUGIN_NAME}")
                        ipb_plugin_enable_tests("ipb-sink-${PLUGIN_NAME}")
                    endif()
                    if(TARGET "ipb-scoop-${PLUGIN_NAME}")
                        ipb_plugin_enable_tests("ipb-scoop-${PLUGIN_NAME}")
                    endif()
                endif()
            endif()
        endif()
    endforeach()

endfunction()
