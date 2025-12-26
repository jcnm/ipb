cmake_minimum_required(VERSION 3.20)

# Find Google Test
find_package(GTest QUIET)

if(NOT GTest_FOUND)
    message(STATUS "GTest not found, fetching from GitHub...")
    include(FetchContent)
    FetchContent_Declare(
        googletest
        GIT_REPOSITORY https://github.com/google/googletest.git
        GIT_TAG v1.14.0
    )
    FetchContent_MakeAvailable(googletest)
endif()

# Transform tests
add_executable(transform_tests
    transform_test.cpp
)

target_link_libraries(transform_tests
    PRIVATE
        ipb-transform
        ipb-common
        GTest::gtest
        GTest::gtest_main
)

target_compile_features(transform_tests PRIVATE cxx_std_20)

# Add test
include(GoogleTest)
gtest_discover_tests(transform_tests)
