# IPB Core Components Tests
cmake_minimum_required(VERSION 3.16)

# Find GTest
find_package(GTest QUIET)

if(GTest_FOUND)
    message(STATUS "GTest found - enabling IPB Core Components tests")

    # Pattern Matching Tests
    add_executable(test_pattern_matching
        test_pattern_matching.cpp
    )

    target_link_libraries(test_pattern_matching
        ipb::core-components
        ipb::common
        GTest::gtest
        GTest::gtest_main
    )
    if(NOT WIN32)
        target_link_libraries(test_pattern_matching pthread)
    endif()

    target_compile_features(test_pattern_matching PRIVATE cxx_std_20)

    # Add test discovery
    include(GoogleTest)
    gtest_discover_tests(test_pattern_matching
        PROPERTIES
            TIMEOUT 60
    )

    message(STATUS "IPB Core Components tests enabled")
else()
    message(STATUS "GTest not found - IPB Core Components tests disabled")
endif()
