cmake_minimum_required(VERSION 3.20)
project(libipb-core-components VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Options
option(IPB_CORE_USE_CTRE "Use compile-time regex (CTRE) for pattern matching" ON)
option(IPB_CORE_ENABLE_BENCHMARKS "Enable performance benchmarks" OFF)

# Find CTRE if enabled (header-only library)
if(IPB_CORE_USE_CTRE)
    find_path(CTRE_INCLUDE_DIR ctre.hpp
        HINTS
            ${CMAKE_PREFIX_PATH}/include
            /usr/local/include
            /usr/include
    )
    if(CTRE_INCLUDE_DIR)
        message(STATUS "CTRE found at ${CTRE_INCLUDE_DIR}")
        add_compile_definitions(IPB_HAS_CTRE)
    else()
        message(STATUS "CTRE not found - using std::regex fallback")
    endif()
endif()

# Find dependencies for config loader
find_package(yaml-cpp REQUIRED)
find_package(PkgConfig REQUIRED)

# Find jsoncpp
if(NOT TARGET jsoncpp_lib AND NOT TARGET JsonCpp::JsonCpp)
    find_package(jsoncpp QUIET)
    if(NOT jsoncpp_FOUND)
        pkg_check_modules(JSONCPP REQUIRED jsoncpp)
    endif()
endif()

# Library sources
set(SOURCES
    # Message Bus
    src/message_bus/message_bus.cpp
    src/message_bus/channel.cpp

    # Rule Engine
    src/rule_engine/rule_engine.cpp
    src/rule_engine/pattern_matcher.cpp
    src/rule_engine/compiled_pattern_cache.cpp

    # EDF Scheduler
    src/scheduler/edf_scheduler.cpp
    src/scheduler/task_queue.cpp

    # Sink Registry
    src/sink_registry/sink_registry.cpp
    src/sink_registry/load_balancer.cpp

    # Scoop Registry
    src/scoop_registry/scoop_registry.cpp

    # Config Loader
    src/config/config_loader.cpp
)

add_library(ipb-core-components ${SOURCES})
add_library(ipb::core-components ALIAS ipb-core-components)

target_include_directories(ipb-core-components
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${JSONCPP_INCLUDE_DIRS}
)

if(CTRE_INCLUDE_DIR)
    target_include_directories(ipb-core-components SYSTEM PUBLIC ${CTRE_INCLUDE_DIR})
endif()

# Link dependencies
target_link_libraries(ipb-core-components
    PUBLIC
        ipb::common
    PRIVATE
        pthread
        yaml-cpp
        jsoncpp_lib
)

# Compiler flags for real-time performance
target_compile_options(ipb-core-components PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall -Wextra -Wpedantic>
    $<$<CXX_COMPILER_ID:GNU,Clang>:-fno-rtti>  # Reduce binary size
)

# Export configuration
install(TARGETS ipb-core-components
    EXPORT ipb-core-components-targets
    LIBRARY DESTINATION lib
    ARCHIVE DESTINATION lib
    RUNTIME DESTINATION bin
    INCLUDES DESTINATION include
)

install(DIRECTORY include/
    DESTINATION include
)

install(EXPORT ipb-core-components-targets
    FILE ipb-core-components-config.cmake
    NAMESPACE ipb::
    DESTINATION lib/cmake/ipb-core-components
)
