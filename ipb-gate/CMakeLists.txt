# IPB Gate - Main Application
cmake_minimum_required(VERSION 3.20)

project(ipb-gate
    VERSION 1.0.0
    DESCRIPTION "IPB Gate - Industrial Protocol Bridge Gateway Application"
    LANGUAGES CXX
)

# Find required dependencies
find_package(yaml-cpp REQUIRED)
find_package(Threads REQUIRED)

# Create the main executable
add_executable(ipb-gate
    src/main.cpp
    src/orchestrator.cpp
    src/config_loader.cpp
    src/signal_handler.cpp
    src/daemon_utils.cpp
)

# Include directories
target_include_directories(ipb-gate PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/libipb-common/include
    ${CMAKE_SOURCE_DIR}/libipb-router/include
    ${CMAKE_SOURCE_DIR}/libipb-core-components/include
    ${CMAKE_SOURCE_DIR}/libipb-sink-console/include
    ${CMAKE_SOURCE_DIR}/libipb-sink-syslog/include
)

# Link libraries
target_link_libraries(ipb-gate PRIVATE
    ipb-common
    ipb-router
    yaml-cpp
    Threads::Threads
    ${CMAKE_DL_LIBS}
)

# Conditionally link adapters and sinks
if(ENABLE_MODBUS_ADAPTER)
    target_link_libraries(ipb-gate PRIVATE ipb-adapter-modbus)
endif()

if(ENABLE_OPCUA_ADAPTER)
    target_link_libraries(ipb-gate PRIVATE ipb-adapter-opcua)
endif()

if(ENABLE_MQTT_ADAPTER)
    target_link_libraries(ipb-gate PRIVATE ipb-adapter-mqtt)
endif()

if(ENABLE_KAFKA_SINK)
    target_link_libraries(ipb-gate PRIVATE ipb-sink-kafka)
endif()

if(ENABLE_ZMQ_SINK)
    target_link_libraries(ipb-gate PRIVATE ipb-sink-zmq)
endif()

if(ENABLE_CONSOLE_SINK)
    target_link_libraries(ipb-gate PRIVATE ipb-sink-console)
endif()

if(ENABLE_SYSLOG_SINK)
    target_link_libraries(ipb-gate PRIVATE ipb-sink-syslog)
endif()

# Compiler-specific options
target_compile_features(ipb-gate PRIVATE cxx_std_20)

# Real-time optimizations
if(ENABLE_REALTIME_OPTIMIZATIONS)
    target_compile_definitions(ipb-gate PRIVATE IPB_REALTIME_ENABLED)
    
    # Link real-time library on Linux
    if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
        target_link_libraries(ipb-gate PRIVATE rt)
    endif()
endif()

# Performance optimizations
if(CMAKE_BUILD_TYPE STREQUAL "Release")
    target_compile_options(ipb-gate PRIVATE
        -ffast-math
        -funroll-loops
        -finline-functions
    )
    
    # Link-time optimization
    if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU" OR CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
        target_compile_options(ipb-gate PRIVATE -flto)
        target_link_options(ipb-gate PRIVATE -flto)
    endif()
endif()

# Platform-specific libraries
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    target_link_libraries(ipb-gate PRIVATE pthread dl)
elseif(CMAKE_SYSTEM_NAME STREQUAL "Windows")
    target_link_libraries(ipb-gate PRIVATE ws2_32 wsock32)
endif()

# Installation
install(TARGETS ipb-gate
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

# Install configuration files
install(FILES
    config/ipb-gate.yaml.example
    config/adapters.yaml.example
    config/sinks.yaml.example
    config/routing.yaml.example
    DESTINATION ${CMAKE_INSTALL_SYSCONFDIR}/ipb
)

# Install systemd service file (Linux only)
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    install(FILES
        systemd/ipb-gate.service
        DESTINATION ${CMAKE_INSTALL_PREFIX}/lib/systemd/system
    )
endif()

# Install shell completion scripts
install(FILES
    completion/ipb-gate.bash
    DESTINATION ${CMAKE_INSTALL_DATADIR}/bash-completion/completions
    RENAME ipb-gate
)

# Create man page
if(ENABLE_DOCUMENTATION)
    find_program(GZIP_EXE NAMES "gzip")
    if(GZIP_EXE)
        add_custom_command(
            OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/ipb-gate.1.gz
            COMMAND ${GZIP_EXE} -c ${CMAKE_CURRENT_SOURCE_DIR}/man/ipb-gate.1 > ${CMAKE_CURRENT_BINARY_DIR}/ipb-gate.1.gz
            DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/man/ipb-gate.1
        )
        
        add_custom_target(man ALL DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/ipb-gate.1.gz)
        
        install(FILES ${CMAKE_CURRENT_BINARY_DIR}/ipb-gate.1.gz
            DESTINATION ${CMAKE_INSTALL_MANDIR}/man1
        )
    endif()
endif()

# Development tools
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    # Add debug symbols
    target_compile_options(ipb-gate PRIVATE -g3)
    
    # Enable runtime checks
    target_compile_definitions(ipb-gate PRIVATE
        IPB_DEBUG_ENABLED
        IPB_RUNTIME_CHECKS_ENABLED
    )
endif()

# Static analysis
if(ENABLE_STATIC_ANALYSIS)
    set_target_properties(ipb-gate PROPERTIES
        CXX_CLANG_TIDY "${CLANG_TIDY_EXE};-checks=*,-fuchsia-*,-google-*,-zircon-*,-abseil-*,-llvm-*"
    )
endif()

# Code coverage
if(ENABLE_COVERAGE)
    target_compile_options(ipb-gate PRIVATE --coverage)
    target_link_options(ipb-gate PRIVATE --coverage)
endif()

# Profiling
if(ENABLE_PROFILING)
    target_compile_options(ipb-gate PRIVATE -pg)
    target_link_options(ipb-gate PRIVATE -pg)
endif()

# Memory debugging
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    option(ENABLE_ASAN "Enable AddressSanitizer" OFF)
    option(ENABLE_TSAN "Enable ThreadSanitizer" OFF)
    option(ENABLE_UBSAN "Enable UndefinedBehaviorSanitizer" OFF)
    
    if(ENABLE_ASAN)
        target_compile_options(ipb-gate PRIVATE -fsanitize=address)
        target_link_options(ipb-gate PRIVATE -fsanitize=address)
    endif()
    
    if(ENABLE_TSAN)
        target_compile_options(ipb-gate PRIVATE -fsanitize=thread)
        target_link_options(ipb-gate PRIVATE -fsanitize=thread)
    endif()
    
    if(ENABLE_UBSAN)
        target_compile_options(ipb-gate PRIVATE -fsanitize=undefined)
        target_link_options(ipb-gate PRIVATE -fsanitize=undefined)
    endif()
endif()

# Custom targets for development
add_custom_target(run-ipb-gate
    COMMAND $<TARGET_FILE:ipb-gate> -c ${CMAKE_CURRENT_SOURCE_DIR}/config/ipb-gate.yaml.example -t
    DEPENDS ipb-gate
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
    COMMENT "Running ipb-gate with example configuration"
)

add_custom_target(debug-ipb-gate
    COMMAND gdb --args $<TARGET_FILE:ipb-gate> -c ${CMAKE_CURRENT_SOURCE_DIR}/config/ipb-gate.yaml.example
    DEPENDS ipb-gate
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
    COMMENT "Debugging ipb-gate with GDB"
)

# Performance testing
if(ENABLE_BENCHMARKS)
    add_custom_target(perf-ipb-gate
        COMMAND perf record -g $<TARGET_FILE:ipb-gate> -c ${CMAKE_CURRENT_SOURCE_DIR}/config/ipb-gate.yaml.example
        DEPENDS ipb-gate
        WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
        COMMENT "Performance profiling with perf"
    )
endif()

# Package configuration
set(CPACK_PACKAGE_NAME "ipb-gate")
set(CPACK_PACKAGE_VERSION ${PROJECT_VERSION})
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Industrial Protocol Bridge Gateway")
set(CPACK_PACKAGE_VENDOR "IPB Project")
set(CPACK_PACKAGE_CONTACT "support@ipb-project.org")

# DEB package configuration
set(CPACK_DEBIAN_PACKAGE_DEPENDS "libyaml-cpp0.7, librdkafka++1, libzmq5, libmodbus5, libopen62541-1")
set(CPACK_DEBIAN_PACKAGE_SECTION "net")
set(CPACK_DEBIAN_PACKAGE_PRIORITY "optional")

# RPM package configuration
set(CPACK_RPM_PACKAGE_GROUP "Applications/Internet")
set(CPACK_RPM_PACKAGE_REQUIRES "yaml-cpp, librdkafka, zeromq, libmodbus, open62541")

include(CPack)

