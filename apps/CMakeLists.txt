# IPB Applications
cmake_minimum_required(VERSION 3.16)

# Application build options
option(IPB_BUILD_GATE "Build IPB Gate (server application)" ON)
option(IPB_BUILD_BRIDGE "Build IPB Bridge (edge/embedded application)" ON)

# IPB Gate - Full-featured server application
# Note: ipb-gate uses POSIX-specific features (daemonization, fork, etc.)
# and is not supported on Windows
if(IPB_BUILD_GATE AND NOT WIN32)
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/ipb-gate/CMakeLists.txt")
        add_subdirectory(ipb-gate)
    endif()
elseif(IPB_BUILD_GATE AND WIN32)
    message(STATUS "ipb-gate: Skipping on Windows (requires POSIX features)")
endif()

# IPB Bridge - Lightweight edge/embedded application
if(IPB_BUILD_BRIDGE)
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/ipb-bridge/CMakeLists.txt")
        add_subdirectory(ipb-bridge)
    endif()
endif()
