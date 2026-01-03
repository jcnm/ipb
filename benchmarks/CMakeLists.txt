# IPB Benchmark Suite CMakeLists.txt

cmake_minimum_required(VERSION 3.20)

project(ipb-benchmarks
    VERSION 1.0.0
    DESCRIPTION "IPB Performance Benchmark Suite"
    LANGUAGES CXX
)

# C++ Standard
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Find required packages
find_package(Threads REQUIRED)

# Benchmark executable
add_executable(ipb-benchmark
    src/main.cpp
)

target_include_directories(ipb-benchmark
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${CMAKE_SOURCE_DIR}/core/common/include
)

target_link_libraries(ipb-benchmark
    PRIVATE
        ipb-common
        Threads::Threads
)

# Optimization flags for benchmarks
if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU" OR CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
    target_compile_options(ipb-benchmark PRIVATE
        -O3
        -DNDEBUG
        -fno-omit-frame-pointer  # Keep for profiling
    )
    if(NOT APPLE)
        target_compile_options(ipb-benchmark PRIVATE -march=native)
    endif()
elseif(CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")
    target_compile_options(ipb-benchmark PRIVATE
        /O2
        /DNDEBUG
    )
endif()

# Create output directories
file(MAKE_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/results)
file(MAKE_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/baselines)
file(MAKE_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/reports)

# Install
install(TARGETS ipb-benchmark
    RUNTIME DESTINATION bin
)

# Custom targets for running benchmarks
add_custom_target(benchmark
    COMMAND ipb-benchmark --verbose
    DEPENDS ipb-benchmark
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    COMMENT "Running all IPB benchmarks"
)

add_custom_target(benchmark-core
    COMMAND ipb-benchmark --category=core --verbose
    DEPENDS ipb-benchmark
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    COMMENT "Running core benchmarks"
)

add_custom_target(benchmark-sinks
    COMMAND ipb-benchmark --category=sinks --verbose
    DEPENDS ipb-benchmark
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    COMMENT "Running sink benchmarks"
)

add_custom_target(benchmark-scoops
    COMMAND ipb-benchmark --category=scoops --verbose
    DEPENDS ipb-benchmark
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    COMMENT "Running scoop benchmarks"
)

add_custom_target(benchmark-transports
    COMMAND ipb-benchmark --category=transports --verbose
    DEPENDS ipb-benchmark
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    COMMENT "Running transport benchmarks"
)

add_custom_target(benchmark-report
    COMMAND ipb-benchmark --verbose --report
    DEPENDS ipb-benchmark
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    COMMENT "Running benchmarks and generating report"
)

# Print configuration
message(STATUS "")
message(STATUS "IPB Benchmark Suite Configuration:")
message(STATUS "  Build type: ${CMAKE_BUILD_TYPE}")
message(STATUS "  C++ standard: ${CMAKE_CXX_STANDARD}")
message(STATUS "  Output directory: ${CMAKE_CURRENT_SOURCE_DIR}/results")
message(STATUS "")
