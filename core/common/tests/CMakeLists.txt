# IPB Common Tests
cmake_minimum_required(VERSION 3.16)

# Find GTest (optional)
find_package(GTest QUIET)

if(GTest_FOUND)
    # Test executable
    add_executable(test_ipb_common
        test_data_point.cpp
    )
    
    target_link_libraries(test_ipb_common
        ipb-common
        GTest::gtest
        GTest::gtest_main
    )
    
    # Add test
    # Use PRE_TEST discovery mode to avoid running test binary during build
    # This is required on Windows where DLLs may not be in PATH during build
    include(GoogleTest)
    gtest_discover_tests(test_ipb_common
        DISCOVERY_MODE PRE_TEST
    )
    
    message(STATUS "IPB Common tests enabled")
else()
    message(STATUS "GTest not found. IPB Common tests disabled.")
endif()

