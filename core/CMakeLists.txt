# IPB Core Components
# Aggregates: common, router, security

# Common library (always built)
add_subdirectory(common)

# Router (depends on common)
add_subdirectory(router)

# Security/TLS layer (depends on common)
add_subdirectory(security)

# Transform layer (bijective compression/encryption pipeline)
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/transform/CMakeLists.txt")
    add_subdirectory(transform)
endif()

# Core components (optional)
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/components/CMakeLists.txt")
    add_subdirectory(components)
endif()
