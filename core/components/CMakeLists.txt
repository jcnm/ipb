cmake_minimum_required(VERSION 3.20)
project(libipb-core-components VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Options
option(IPB_CORE_USE_CTRE "Use compile-time regex (CTRE) for pattern matching" ON)
option(IPB_CORE_ENABLE_BENCHMARKS "Enable performance benchmarks" OFF)
option(IPB_CONFIG_EMBEDDED_LOADER "Include embedded config loader" ON)

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

# ==============================================================================
# EMBEDDED PLATFORM: Use rapidyaml (ryml) + cJSON
# Uses IPB_BUILD_MODE from cmake/IPBOptions.cmake
# ==============================================================================
if(IPB_BUILD_MODE STREQUAL "EMBEDDED")
    message(STATUS "Config Loader: EMBEDDED mode - using ryml + cJSON")
    # Define to indicate embedded build mode (regardless of whether ryml/cJSON are found)
    add_compile_definitions(IPB_BUILD_EMBEDDED)

    # Find rapidyaml (ryml) - header-only or compiled
    find_path(RYML_INCLUDE_DIR ryml.hpp
        HINTS
            ${CMAKE_PREFIX_PATH}/include
            /usr/local/include
            /usr/include
    )
    if(RYML_INCLUDE_DIR)
        message(STATUS "rapidyaml found at ${RYML_INCLUDE_DIR}")
        add_compile_definitions(IPB_CONFIG_USE_RYML)
        set(RYML_FOUND TRUE)
    else()
        # Try pkg-config
        find_package(PkgConfig QUIET)
        if(PkgConfig_FOUND)
            pkg_check_modules(RYML QUIET ryml)
            if(RYML_FOUND)
                message(STATUS "rapidyaml found via pkg-config")
                add_compile_definitions(IPB_CONFIG_USE_RYML)
            endif()
        endif()
    endif()

    # Find cJSON
    find_path(CJSON_INCLUDE_DIR cjson/cJSON.h
        HINTS
            ${CMAKE_PREFIX_PATH}/include
            /usr/local/include
            /usr/include
    )
    find_library(CJSON_LIBRARY cjson
        HINTS
            ${CMAKE_PREFIX_PATH}/lib
            /usr/local/lib
            /usr/lib
    )
    if(CJSON_INCLUDE_DIR AND CJSON_LIBRARY)
        message(STATUS "cJSON found at ${CJSON_INCLUDE_DIR}")
        add_compile_definitions(IPB_CONFIG_USE_CJSON)
        set(CJSON_FOUND TRUE)
    else()
        # Try pkg-config
        if(PkgConfig_FOUND)
            pkg_check_modules(CJSON QUIET libcjson)
            if(CJSON_FOUND)
                message(STATUS "cJSON found via pkg-config")
                add_compile_definitions(IPB_CONFIG_USE_CJSON)
            endif()
        endif()
    endif()

    # Fallback warning
    if(NOT RYML_FOUND)
        message(WARNING "rapidyaml not found - embedded YAML parsing disabled")
    endif()
    if(NOT CJSON_FOUND)
        message(WARNING "cJSON not found - embedded JSON parsing disabled")
    endif()

# ==============================================================================
# SERVER/EDGE PLATFORM: Use yaml-cpp + jsoncpp (default)
# ==============================================================================
else()
    message(STATUS "Config Loader: ${IPB_BUILD_MODE} mode - using yaml-cpp + jsoncpp")

    # Find dependencies for config loader
    # PkgConfig is optional on Windows
    if(NOT WIN32)
        find_package(PkgConfig REQUIRED)
    else()
        find_package(PkgConfig QUIET)
    endif()

    # Find yaml-cpp (check if already found by parent CMakeLists.txt)
    if(NOT TARGET yaml-cpp)
        find_package(yaml-cpp REQUIRED)
    endif()

    # On macOS, yaml-cpp CMake config may not have proper include directories
    if(APPLE AND TARGET yaml-cpp)
        get_target_property(_yaml_inc yaml-cpp INTERFACE_INCLUDE_DIRECTORIES)
        if(NOT _yaml_inc OR "${_yaml_inc}" STREQUAL "_yaml_inc-NOTFOUND")
            find_path(YAML_CPP_COMPONENTS_INCLUDE_DIR yaml-cpp/yaml.h
                PATHS
                    /opt/homebrew/include
                    /usr/local/include
                    /usr/include
                NO_DEFAULT_PATH
            )
            if(YAML_CPP_COMPONENTS_INCLUDE_DIR)
                message(STATUS "ipb-core-components: Found yaml-cpp includes at ${YAML_CPP_COMPONENTS_INCLUDE_DIR}")
            endif()
        endif()
    endif()

    # Find jsoncpp
    if(NOT TARGET jsoncpp_lib AND NOT TARGET JsonCpp::JsonCpp)
        find_package(jsoncpp QUIET)
        if(NOT jsoncpp_FOUND AND PkgConfig_FOUND)
            pkg_check_modules(JSONCPP REQUIRED jsoncpp)
        endif()
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
)

# Config Loader: select platform-appropriate implementation
if(IPB_BUILD_MODE STREQUAL "EMBEDDED")
    # EMBEDDED mode: use embedded config loader (ryml + cJSON)
    list(APPEND SOURCES src/config/config_loader_embedded.cpp)
    message(STATUS "Config loader: embedded implementation (ryml + cJSON)")
else()
    # Server/Edge mode: use full config loader (yaml-cpp + jsoncpp)
    list(APPEND SOURCES src/config/config_loader.cpp)
    message(STATUS "Config loader: full implementation (yaml-cpp + jsoncpp)")
endif()

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
)
if(NOT WIN32)
    target_link_libraries(ipb-core-components PRIVATE pthread)
endif()

# Platform-specific library linking
if(IPB_BUILD_MODE STREQUAL "EMBEDDED")
    # Embedded: rapidyaml + cJSON
    if(RYML_FOUND)
        if(RYML_INCLUDE_DIR)
            target_include_directories(ipb-core-components PRIVATE ${RYML_INCLUDE_DIR})
        endif()
        if(RYML_LIBRARIES)
            target_link_libraries(ipb-core-components PRIVATE ${RYML_LIBRARIES})
        endif()
    endif()
    if(CJSON_FOUND)
        if(CJSON_INCLUDE_DIR)
            target_include_directories(ipb-core-components PRIVATE ${CJSON_INCLUDE_DIR})
        endif()
        if(CJSON_LIBRARY)
            target_link_libraries(ipb-core-components PRIVATE ${CJSON_LIBRARY})
        elseif(CJSON_LIBRARIES)
            target_link_libraries(ipb-core-components PRIVATE ${CJSON_LIBRARIES})
        endif()
    endif()
else()
    # Server/Edge: yaml-cpp + jsoncpp
    target_link_libraries(ipb-core-components PRIVATE yaml-cpp jsoncpp_lib)
    # Add explicit include directory on macOS if needed
    if(YAML_CPP_COMPONENTS_INCLUDE_DIR)
        target_include_directories(ipb-core-components PRIVATE ${YAML_CPP_COMPONENTS_INCLUDE_DIR})
    endif()
endif()

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

# Tests
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tests")
    add_subdirectory(tests)
endif()
