# IPB E2E Tests
cmake_minimum_required(VERSION 3.20)

# E2E test executable
add_executable(ipb_e2e_tests
    test_e2e_routing.cpp
)

target_include_directories(ipb_e2e_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/core/common/include
    ${CMAKE_SOURCE_DIR}/core/components/include
    ${CMAKE_SOURCE_DIR}/core/router/include
)

target_link_libraries(ipb_e2e_tests PRIVATE
    GTest::gtest
    GTest::gtest_main
    ipb_router
    ipb_common
    Threads::Threads
)

# Register E2E tests with CTest
include(GoogleTest)
gtest_discover_tests(ipb_e2e_tests
    PROPERTIES
        LABELS "e2e"
        TIMEOUT 120
)

# Custom target for E2E tests only
add_custom_target(test-e2e
    COMMAND ${CMAKE_CTEST_COMMAND} --output-on-failure -L e2e
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
    COMMENT "Running E2E tests"
)
