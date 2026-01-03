# IPB Security Library - TLS Abstraction Layer
cmake_minimum_required(VERSION 3.16)

project(libipb-security VERSION 1.0.0 LANGUAGES CXX)

# C++ Standard
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Find dependencies
find_package(Threads REQUIRED)

# Determine SSL backend from parent if available
if(NOT DEFINED IPB_SSL_BACKEND_RESOLVED)
    set(IPB_SSL_BACKEND_RESOLVED "openssl")
endif()

# Find SSL library based on backend
set(SECURITY_SOURCES)
set(SECURITY_LIBRARIES)
set(SECURITY_COMPILE_DEFINITIONS)

if(IPB_SSL_BACKEND_RESOLVED STREQUAL "openssl")
    find_package(OpenSSL QUIET)
    if(OpenSSL_FOUND)
        list(APPEND SECURITY_SOURCES src/tls_openssl.cpp)
        list(APPEND SECURITY_LIBRARIES OpenSSL::SSL OpenSSL::Crypto)
        list(APPEND SECURITY_COMPILE_DEFINITIONS IPB_SSL_OPENSSL)
        message(STATUS "Security backend: OpenSSL ${OPENSSL_VERSION}")
    else()
        message(WARNING "OpenSSL not found, security features disabled")
        list(APPEND SECURITY_COMPILE_DEFINITIONS IPB_SSL_NONE)
    endif()

elseif(IPB_SSL_BACKEND_RESOLVED STREQUAL "mbedtls")
    find_package(MbedTLS QUIET)
    if(MbedTLS_FOUND)
        list(APPEND SECURITY_SOURCES src/tls_mbedtls.cpp)
        list(APPEND SECURITY_LIBRARIES MbedTLS::mbedtls MbedTLS::mbedcrypto MbedTLS::mbedx509)
        list(APPEND SECURITY_COMPILE_DEFINITIONS IPB_SSL_MBEDTLS)
        message(STATUS "Security backend: mbedTLS")
    else()
        # Fallback to pkg-config
        find_package(PkgConfig)
        if(PkgConfig_FOUND)
            pkg_check_modules(MBEDTLS mbedtls mbedcrypto mbedx509)
            if(MBEDTLS_FOUND)
                list(APPEND SECURITY_SOURCES src/tls_mbedtls.cpp)
                list(APPEND SECURITY_LIBRARIES ${MBEDTLS_LIBRARIES})
                list(APPEND SECURITY_COMPILE_DEFINITIONS IPB_SSL_MBEDTLS)
                message(STATUS "Security backend: mbedTLS (pkg-config)")
            else()
                message(WARNING "mbedTLS not found, falling back to OpenSSL")
                find_package(OpenSSL QUIET)
                if(OpenSSL_FOUND)
                    list(APPEND SECURITY_SOURCES src/tls_openssl.cpp)
                    list(APPEND SECURITY_LIBRARIES OpenSSL::SSL OpenSSL::Crypto)
                    list(APPEND SECURITY_COMPILE_DEFINITIONS IPB_SSL_OPENSSL)
                endif()
            endif()
        endif()
    endif()

elseif(IPB_SSL_BACKEND_RESOLVED STREQUAL "wolfssl")
    find_package(PkgConfig)
    if(PkgConfig_FOUND)
        pkg_check_modules(WOLFSSL wolfssl)
        if(WOLFSSL_FOUND)
            list(APPEND SECURITY_SOURCES src/tls_wolfssl.cpp)
            list(APPEND SECURITY_LIBRARIES ${WOLFSSL_LIBRARIES})
            list(APPEND SECURITY_COMPILE_DEFINITIONS IPB_SSL_WOLFSSL)
            message(STATUS "Security backend: wolfSSL")
        else()
            message(WARNING "wolfSSL not found, falling back to OpenSSL")
            find_package(OpenSSL QUIET)
            if(OpenSSL_FOUND)
                list(APPEND SECURITY_SOURCES src/tls_openssl.cpp)
                list(APPEND SECURITY_LIBRARIES OpenSSL::SSL OpenSSL::Crypto)
                list(APPEND SECURITY_COMPILE_DEFINITIONS IPB_SSL_OPENSSL)
            endif()
        endif()
    endif()

else()
    list(APPEND SECURITY_COMPILE_DEFINITIONS IPB_SSL_NONE)
    message(STATUS "Security backend: None (TLS disabled)")
endif()

# Create library (even if empty, for consistent build)
if(NOT SECURITY_SOURCES)
    # Create a stub source file
    file(WRITE ${CMAKE_CURRENT_BINARY_DIR}/tls_stub.cpp
        "// Stub file - no TLS backend configured\n"
        "namespace ipb::security { void stub() {} }\n")
    list(APPEND SECURITY_SOURCES ${CMAKE_CURRENT_BINARY_DIR}/tls_stub.cpp)
endif()

add_library(ipb-security STATIC ${SECURITY_SOURCES})

# Set target properties
set_target_properties(ipb-security PROPERTIES
    VERSION ${PROJECT_VERSION}
    SOVERSION ${PROJECT_VERSION_MAJOR}
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED ON
)

# Include directories
target_include_directories(ipb-security
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

# Link libraries
target_link_libraries(ipb-security
    PUBLIC
        ipb-common
        Threads::Threads
    PRIVATE
        ${SECURITY_LIBRARIES}
)

# Compile definitions
target_compile_definitions(ipb-security PRIVATE ${SECURITY_COMPILE_DEFINITIONS})

# Compiler flags (platform-specific)
if(MSVC)
    target_compile_options(ipb-security PRIVATE
        /W4
        $<$<CONFIG:Release>:/O2>
        $<$<CONFIG:Debug>:/Od /Zi>
    )
else()
    target_compile_options(ipb-security PRIVATE
        -Wall -Wextra -Wpedantic
        $<$<CONFIG:Release>:-O3>
        $<$<CONFIG:Debug>:-g -O0>
    )
endif()

# Install targets
install(TARGETS ipb-security
    EXPORT ipb-security-targets
    LIBRARY DESTINATION lib
    ARCHIVE DESTINATION lib
    RUNTIME DESTINATION bin
)

# Install headers
install(DIRECTORY include/
    DESTINATION include
    FILES_MATCHING PATTERN "*.hpp"
)

# Install export
install(EXPORT ipb-security-targets
    FILE ipb-security-targets.cmake
    NAMESPACE ipb::
    DESTINATION lib/cmake/ipb-security
)

# Create config files
include(CMakePackageConfigHelpers)

configure_package_config_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/ipb-security-config.cmake.in"
    "${CMAKE_CURRENT_BINARY_DIR}/ipb-security-config.cmake"
    INSTALL_DESTINATION lib/cmake/ipb-security
)

write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/ipb-security-config-version.cmake"
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion
)

install(FILES
    "${CMAKE_CURRENT_BINARY_DIR}/ipb-security-config.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/ipb-security-config-version.cmake"
    DESTINATION lib/cmake/ipb-security
)

# Summary
message(STATUS "")
message(STATUS "IPB Security Library Configuration:")
message(STATUS "  Version: ${PROJECT_VERSION}")
message(STATUS "  SSL Backend: ${IPB_SSL_BACKEND_RESOLVED}")
message(STATUS "  Sources: ${SECURITY_SOURCES}")
message(STATUS "")
