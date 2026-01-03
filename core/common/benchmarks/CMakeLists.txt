# Performance Benchmarks CMakeLists.txt

cmake_minimum_required(VERSION 3.20)

# Benchmark executable
add_executable(ipb-benchmarks
    run_benchmarks.cpp
)

target_include_directories(ipb-benchmarks
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/../include
)

target_link_libraries(ipb-benchmarks
    PRIVATE
        ipb-common
        Threads::Threads
)

# Compiler-specific optimization flags
if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU" OR CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
    target_compile_options(ipb-benchmarks PRIVATE -O3 -DNDEBUG)
    if(NOT APPLE)
        target_compile_options(ipb-benchmarks PRIVATE -march=native)
    endif()
elseif(MSVC)
    target_compile_options(ipb-benchmarks PRIVATE /O2 /DNDEBUG)
endif()

# Install benchmark binary
install(TARGETS ipb-benchmarks
    RUNTIME DESTINATION bin
)
