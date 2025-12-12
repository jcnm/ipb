# IPBOptions.cmake - IPB Build Mode and User Options
# This file defines build modes (SERVER, EDGE, EMBEDDED) and allows user overrides

#=============================================================================
# Build Mode Selection
#=============================================================================
set(IPB_BUILD_MODE "SERVER" CACHE STRING "Build mode: SERVER, EDGE, EMBEDDED")
set_property(CACHE IPB_BUILD_MODE PROPERTY STRINGS SERVER EDGE EMBEDDED)

# Validate build mode
if(NOT IPB_BUILD_MODE MATCHES "^(SERVER|EDGE|EMBEDDED)$")
    message(FATAL_ERROR "Invalid IPB_BUILD_MODE: ${IPB_BUILD_MODE}. Must be SERVER, EDGE, or EMBEDDED")
endif()

#=============================================================================
# Default Values Per Mode
#=============================================================================
# These are the defaults - users can override any of them

if(IPB_BUILD_MODE STREQUAL "SERVER")
    set(_IPB_DEFAULT_BUFFER_SIZE 1048576)        # 1MB
    set(_IPB_DEFAULT_MAX_CONNECTIONS 1000)
    set(_IPB_DEFAULT_THREAD_POOL_SIZE 16)
    set(_IPB_DEFAULT_ENABLE_HTTP2 ON)
    set(_IPB_DEFAULT_ENABLE_STATISTICS ON)
    set(_IPB_DEFAULT_ENABLE_FULL_LOGGING ON)
    set(_IPB_DEFAULT_SSL_BACKEND "openssl")
    set(_IPB_DEFAULT_HTTP_BACKEND "curl")
    set(_IPB_DEFAULT_MQTT_BACKEND "paho-cpp")
    set(_IPB_DEFAULT_WS_BACKEND "beast")
    set(_IPB_DEFAULT_JSON_BACKEND "jsoncpp")
    set(_IPB_DEFAULT_PROTOBUF_BACKEND "protobuf")

elseif(IPB_BUILD_MODE STREQUAL "EDGE")
    set(_IPB_DEFAULT_BUFFER_SIZE 65536)          # 64KB
    set(_IPB_DEFAULT_MAX_CONNECTIONS 100)
    set(_IPB_DEFAULT_THREAD_POOL_SIZE 4)
    set(_IPB_DEFAULT_ENABLE_HTTP2 OFF)
    set(_IPB_DEFAULT_ENABLE_STATISTICS ON)
    set(_IPB_DEFAULT_ENABLE_FULL_LOGGING OFF)
    set(_IPB_DEFAULT_SSL_BACKEND "mbedtls")
    set(_IPB_DEFAULT_HTTP_BACKEND "cpphttplib")
    set(_IPB_DEFAULT_MQTT_BACKEND "paho-c")
    set(_IPB_DEFAULT_WS_BACKEND "ixwebsocket")
    set(_IPB_DEFAULT_JSON_BACKEND "nlohmann")
    set(_IPB_DEFAULT_PROTOBUF_BACKEND "protobuf-lite")

elseif(IPB_BUILD_MODE STREQUAL "EMBEDDED")
    set(_IPB_DEFAULT_BUFFER_SIZE 4096)           # 4KB
    set(_IPB_DEFAULT_MAX_CONNECTIONS 10)
    set(_IPB_DEFAULT_THREAD_POOL_SIZE 1)
    set(_IPB_DEFAULT_ENABLE_HTTP2 OFF)
    set(_IPB_DEFAULT_ENABLE_STATISTICS OFF)
    set(_IPB_DEFAULT_ENABLE_FULL_LOGGING OFF)
    set(_IPB_DEFAULT_SSL_BACKEND "mbedtls")
    set(_IPB_DEFAULT_HTTP_BACKEND "lwip")
    set(_IPB_DEFAULT_MQTT_BACKEND "mqtt-c")
    set(_IPB_DEFAULT_WS_BACKEND "lwip")
    set(_IPB_DEFAULT_JSON_BACKEND "cjson")
    set(_IPB_DEFAULT_PROTOBUF_BACKEND "nanopb")
endif()

#=============================================================================
# User-Overridable Options
#=============================================================================

# Performance settings (empty string = use mode default)
set(IPB_BUFFER_SIZE "" CACHE STRING "Buffer size in bytes (empty = mode default)")
set(IPB_MAX_CONNECTIONS "" CACHE STRING "Maximum connections (empty = mode default)")
set(IPB_THREAD_POOL_SIZE "" CACHE STRING "Thread pool size (empty = mode default)")

# Feature toggles (empty string = use mode default)
set(IPB_ENABLE_HTTP2 "" CACHE STRING "Enable HTTP/2 support (ON/OFF, empty = mode default)")
set(IPB_ENABLE_STATISTICS "" CACHE STRING "Enable statistics collection (ON/OFF, empty = mode default)")
set(IPB_ENABLE_FULL_LOGGING "" CACHE STRING "Enable full logging (ON/OFF, empty = mode default)")

# Backend selection (empty string = use mode default)
set(IPB_SSL_BACKEND "" CACHE STRING "SSL backend: openssl, mbedtls, wolfssl, none (empty = mode default)")
set(IPB_HTTP_BACKEND "" CACHE STRING "HTTP backend: curl, cpphttplib, lwip (empty = mode default)")
set(IPB_MQTT_BACKEND "" CACHE STRING "MQTT backend: paho-cpp, paho-c, mqtt-c (empty = mode default)")
set(IPB_WS_BACKEND "" CACHE STRING "WebSocket backend: beast, ixwebsocket, lwip (empty = mode default)")
set(IPB_JSON_BACKEND "" CACHE STRING "JSON backend: jsoncpp, nlohmann, cjson, jsmn (empty = mode default)")
set(IPB_PROTOBUF_BACKEND "" CACHE STRING "Protobuf backend: protobuf, protobuf-lite, nanopb (empty = mode default)")

# Protocol/Feature options
option(IPB_ENABLE_MQTT_TLS "Enable TLS for MQTT transport" ON)
option(IPB_ENABLE_HTTP_TLS "Enable TLS for HTTP transport" ON)
option(IPB_ENABLE_WEBSOCKET "Enable WebSocket support" ON)
option(IPB_ENABLE_OPCUA "Enable OPC UA support" ON)
option(IPB_ENABLE_MODBUS "Enable Modbus support" ON)
option(IPB_ENABLE_SPARKPLUG "Enable Sparkplug B support" ON)
option(IPB_ENABLE_SYSLOG "Enable Syslog support" ON)

#=============================================================================
# Resolve Final Values (user override or mode default)
#=============================================================================

macro(ipb_resolve_option VAR DEFAULT)
    if("${${VAR}}" STREQUAL "")
        set(IPB_${VAR}_RESOLVED ${DEFAULT})
    else()
        set(IPB_${VAR}_RESOLVED ${${VAR}})
    endif()
endmacro()

# Resolve numeric options
if("${IPB_BUFFER_SIZE}" STREQUAL "")
    set(IPB_BUFFER_SIZE_RESOLVED ${_IPB_DEFAULT_BUFFER_SIZE})
else()
    set(IPB_BUFFER_SIZE_RESOLVED ${IPB_BUFFER_SIZE})
endif()

if("${IPB_MAX_CONNECTIONS}" STREQUAL "")
    set(IPB_MAX_CONNECTIONS_RESOLVED ${_IPB_DEFAULT_MAX_CONNECTIONS})
else()
    set(IPB_MAX_CONNECTIONS_RESOLVED ${IPB_MAX_CONNECTIONS})
endif()

if("${IPB_THREAD_POOL_SIZE}" STREQUAL "")
    set(IPB_THREAD_POOL_SIZE_RESOLVED ${_IPB_DEFAULT_THREAD_POOL_SIZE})
else()
    set(IPB_THREAD_POOL_SIZE_RESOLVED ${IPB_THREAD_POOL_SIZE})
endif()

# Resolve boolean options
if("${IPB_ENABLE_HTTP2}" STREQUAL "")
    set(IPB_ENABLE_HTTP2_RESOLVED ${_IPB_DEFAULT_ENABLE_HTTP2})
else()
    set(IPB_ENABLE_HTTP2_RESOLVED ${IPB_ENABLE_HTTP2})
endif()

if("${IPB_ENABLE_STATISTICS}" STREQUAL "")
    set(IPB_ENABLE_STATISTICS_RESOLVED ${_IPB_DEFAULT_ENABLE_STATISTICS})
else()
    set(IPB_ENABLE_STATISTICS_RESOLVED ${IPB_ENABLE_STATISTICS})
endif()

if("${IPB_ENABLE_FULL_LOGGING}" STREQUAL "")
    set(IPB_ENABLE_FULL_LOGGING_RESOLVED ${_IPB_DEFAULT_ENABLE_FULL_LOGGING})
else()
    set(IPB_ENABLE_FULL_LOGGING_RESOLVED ${IPB_ENABLE_FULL_LOGGING})
endif()

# Resolve backend options
if("${IPB_SSL_BACKEND}" STREQUAL "")
    set(IPB_SSL_BACKEND_RESOLVED ${_IPB_DEFAULT_SSL_BACKEND})
else()
    set(IPB_SSL_BACKEND_RESOLVED ${IPB_SSL_BACKEND})
endif()

if("${IPB_HTTP_BACKEND}" STREQUAL "")
    set(IPB_HTTP_BACKEND_RESOLVED ${_IPB_DEFAULT_HTTP_BACKEND})
else()
    set(IPB_HTTP_BACKEND_RESOLVED ${IPB_HTTP_BACKEND})
endif()

if("${IPB_MQTT_BACKEND}" STREQUAL "")
    set(IPB_MQTT_BACKEND_RESOLVED ${_IPB_DEFAULT_MQTT_BACKEND})
else()
    set(IPB_MQTT_BACKEND_RESOLVED ${IPB_MQTT_BACKEND})
endif()

if("${IPB_WS_BACKEND}" STREQUAL "")
    set(IPB_WS_BACKEND_RESOLVED ${_IPB_DEFAULT_WS_BACKEND})
else()
    set(IPB_WS_BACKEND_RESOLVED ${IPB_WS_BACKEND})
endif()

if("${IPB_JSON_BACKEND}" STREQUAL "")
    set(IPB_JSON_BACKEND_RESOLVED ${_IPB_DEFAULT_JSON_BACKEND})
else()
    set(IPB_JSON_BACKEND_RESOLVED ${IPB_JSON_BACKEND})
endif()

if("${IPB_PROTOBUF_BACKEND}" STREQUAL "")
    set(IPB_PROTOBUF_BACKEND_RESOLVED ${_IPB_DEFAULT_PROTOBUF_BACKEND})
else()
    set(IPB_PROTOBUF_BACKEND_RESOLVED ${IPB_PROTOBUF_BACKEND})
endif()

#=============================================================================
# Detect User Overrides (for display in banner)
#=============================================================================

set(IPB_USER_OVERRIDES "")

if(NOT "${IPB_BUFFER_SIZE}" STREQUAL "")
    list(APPEND IPB_USER_OVERRIDES "BUFFER_SIZE")
endif()
if(NOT "${IPB_MAX_CONNECTIONS}" STREQUAL "")
    list(APPEND IPB_USER_OVERRIDES "MAX_CONNECTIONS")
endif()
if(NOT "${IPB_THREAD_POOL_SIZE}" STREQUAL "")
    list(APPEND IPB_USER_OVERRIDES "THREAD_POOL_SIZE")
endif()
if(NOT "${IPB_ENABLE_HTTP2}" STREQUAL "")
    list(APPEND IPB_USER_OVERRIDES "HTTP2")
endif()
if(NOT "${IPB_ENABLE_STATISTICS}" STREQUAL "")
    list(APPEND IPB_USER_OVERRIDES "STATISTICS")
endif()
if(NOT "${IPB_SSL_BACKEND}" STREQUAL "")
    list(APPEND IPB_USER_OVERRIDES "SSL_BACKEND")
endif()
if(NOT "${IPB_HTTP_BACKEND}" STREQUAL "")
    list(APPEND IPB_USER_OVERRIDES "HTTP_BACKEND")
endif()
if(NOT "${IPB_MQTT_BACKEND}" STREQUAL "")
    list(APPEND IPB_USER_OVERRIDES "MQTT_BACKEND")
endif()

#=============================================================================
# Define Compile Definitions
#=============================================================================

# Build mode
if(IPB_BUILD_MODE STREQUAL "SERVER")
    set(IPB_MODE_DEFINE "IPB_MODE_SERVER")
elseif(IPB_BUILD_MODE STREQUAL "EDGE")
    set(IPB_MODE_DEFINE "IPB_MODE_EDGE")
elseif(IPB_BUILD_MODE STREQUAL "EMBEDDED")
    set(IPB_MODE_DEFINE "IPB_MODE_EMBEDDED")
endif()

# SSL backend
string(TOUPPER ${IPB_SSL_BACKEND_RESOLVED} _SSL_UPPER)
set(IPB_SSL_DEFINE "IPB_SSL_${_SSL_UPPER}")

#=============================================================================
# Helper Function to Apply IPB Options to Target
#=============================================================================

function(ipb_configure_target TARGET_NAME)
    # Apply compile definitions
    target_compile_definitions(${TARGET_NAME} PRIVATE
        ${IPB_MODE_DEFINE}=1
        ${IPB_SSL_DEFINE}=1
        IPB_BUFFER_SIZE=${IPB_BUFFER_SIZE_RESOLVED}
        IPB_MAX_CONNECTIONS=${IPB_MAX_CONNECTIONS_RESOLVED}
        IPB_THREAD_POOL_SIZE=${IPB_THREAD_POOL_SIZE_RESOLVED}
    )

    if(IPB_ENABLE_HTTP2_RESOLVED)
        target_compile_definitions(${TARGET_NAME} PRIVATE IPB_ENABLE_HTTP2=1)
    endif()

    if(IPB_ENABLE_STATISTICS_RESOLVED)
        target_compile_definitions(${TARGET_NAME} PRIVATE IPB_ENABLE_STATISTICS=1)
    endif()

    if(IPB_ENABLE_FULL_LOGGING_RESOLVED)
        target_compile_definitions(${TARGET_NAME} PRIVATE IPB_ENABLE_FULL_LOGGING=1)
    endif()

    if(IPB_ENABLE_MQTT_TLS)
        target_compile_definitions(${TARGET_NAME} PRIVATE IPB_MQTT_TLS=1)
    endif()

    if(IPB_ENABLE_HTTP_TLS)
        target_compile_definitions(${TARGET_NAME} PRIVATE IPB_HTTP_TLS=1)
    endif()

    if(IPB_ENABLE_WEBSOCKET)
        target_compile_definitions(${TARGET_NAME} PRIVATE IPB_ENABLE_WEBSOCKET=1)
    endif()
endfunction()
