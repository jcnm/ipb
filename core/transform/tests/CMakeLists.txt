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

# Note: Compression/encryption libraries are linked as PUBLIC in ipb-transform,
# so they automatically propagate to consumers like this test executable.

target_compile_features(transform_tests PRIVATE cxx_std_20)

# Propagate library availability definitions to tests
if(ZSTD_FOUND)
    target_compile_definitions(transform_tests PRIVATE IPB_HAS_ZSTD=1)
endif()
if(LZ4_FOUND)
    target_compile_definitions(transform_tests PRIVATE IPB_HAS_LZ4=1)
endif()
if(SNAPPY_FOUND)
    target_compile_definitions(transform_tests PRIVATE IPB_HAS_SNAPPY=1)
endif()
if(ZLIB_FOUND)
    target_compile_definitions(transform_tests PRIVATE IPB_HAS_ZLIB=1)
endif()
if(OpenSSL_FOUND)
    target_compile_definitions(transform_tests PRIVATE IPB_HAS_OPENSSL=1)
endif()
if(XXHASH_FOUND)
    target_compile_definitions(transform_tests PRIVATE IPB_HAS_XXHASH=1)
endif()

# Add test
# Use PRE_TEST discovery mode to avoid running test binary during build
# This is required on Windows where DLLs may not be in PATH during build
include(GoogleTest)
gtest_discover_tests(transform_tests
    DISCOVERY_MODE PRE_TEST
)
