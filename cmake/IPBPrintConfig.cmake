# IPBPrintConfig.cmake - Configuration Banner Display
# Displays a comprehensive summary of the IPB build configuration

function(ipb_print_config)
    # Helper to format ON/OFF with checkmark
    macro(format_bool VAR OUTPUT)
        if(${VAR})
            set(${OUTPUT} "[ON]")
        else()
            set(${OUTPUT} "[--]")
        endif()
    endmacro()

    # Helper to show if value is user override
    macro(format_value VAR DEFAULT OUTPUT)
        if("${${VAR}}" STREQUAL "")
            set(${OUTPUT} "${DEFAULT}")
        else()
            set(${OUTPUT} "${${VAR}} (override)")
        endif()
    endmacro()

    # Detect platform string
    if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
        set(PLATFORM_STR "Linux ${CMAKE_SYSTEM_PROCESSOR}")
    elseif(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
        set(PLATFORM_STR "macOS ${CMAKE_SYSTEM_PROCESSOR}")
    elseif(CMAKE_SYSTEM_NAME STREQUAL "Windows")
        set(PLATFORM_STR "Windows ${CMAKE_SYSTEM_PROCESSOR}")
    else()
        set(PLATFORM_STR "${CMAKE_SYSTEM_NAME} ${CMAKE_SYSTEM_PROCESSOR}")
    endif()

    # Compiler info
    set(COMPILER_STR "${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}")

    # Build type
    if(CMAKE_BUILD_TYPE)
        set(BUILD_TYPE_STR "${CMAKE_BUILD_TYPE}")
    else()
        set(BUILD_TYPE_STR "Not set")
    endif()

    message(STATUS "")
    message(STATUS "======================================================================")
    message(STATUS "                    IPB Stack Configuration                           ")
    message(STATUS "======================================================================")
    message(STATUS "  Version:        ${PROJECT_VERSION}")
    message(STATUS "  Build Mode:     ${IPB_BUILD_MODE}")
    message(STATUS "  Build Type:     ${BUILD_TYPE_STR}")
    message(STATUS "  Platform:       ${PLATFORM_STR}")
    message(STATUS "  Compiler:       ${COMPILER_STR}")
    message(STATUS "  C++ Standard:   ${CMAKE_CXX_STANDARD}")
    message(STATUS "----------------------------------------------------------------------")

    # User overrides section
    if(IPB_USER_OVERRIDES)
        message(STATUS "  User Overrides: ${IPB_USER_OVERRIDES}")
        message(STATUS "----------------------------------------------------------------------")
    endif()

    # Performance settings
    message(STATUS "  Performance:")
    if(NOT "${IPB_BUFFER_SIZE}" STREQUAL "")
        message(STATUS "    Buffer Size:      ${IPB_BUFFER_SIZE_RESOLVED} bytes (override)")
    else()
        message(STATUS "    Buffer Size:      ${IPB_BUFFER_SIZE_RESOLVED} bytes")
    endif()

    if(NOT "${IPB_MAX_CONNECTIONS}" STREQUAL "")
        message(STATUS "    Max Connections:  ${IPB_MAX_CONNECTIONS_RESOLVED} (override)")
    else()
        message(STATUS "    Max Connections:  ${IPB_MAX_CONNECTIONS_RESOLVED}")
    endif()

    if(NOT "${IPB_THREAD_POOL_SIZE}" STREQUAL "")
        message(STATUS "    Thread Pool:      ${IPB_THREAD_POOL_SIZE_RESOLVED} (override)")
    else()
        message(STATUS "    Thread Pool:      ${IPB_THREAD_POOL_SIZE_RESOLVED}")
    endif()

    message(STATUS "----------------------------------------------------------------------")

    # Backends
    message(STATUS "  Backends:")
    if(NOT "${IPB_SSL_BACKEND}" STREQUAL "")
        message(STATUS "    SSL/TLS:          ${IPB_SSL_BACKEND_RESOLVED} (override)")
    else()
        message(STATUS "    SSL/TLS:          ${IPB_SSL_BACKEND_RESOLVED}")
    endif()

    if(NOT "${IPB_HTTP_BACKEND}" STREQUAL "")
        message(STATUS "    HTTP:             ${IPB_HTTP_BACKEND_RESOLVED} (override)")
    else()
        message(STATUS "    HTTP:             ${IPB_HTTP_BACKEND_RESOLVED}")
    endif()

    if(NOT "${IPB_MQTT_BACKEND}" STREQUAL "")
        message(STATUS "    MQTT:             ${IPB_MQTT_BACKEND_RESOLVED} (override)")
    else()
        message(STATUS "    MQTT:             ${IPB_MQTT_BACKEND_RESOLVED}")
    endif()

    if(IPB_ENABLE_WEBSOCKET)
        if(NOT "${IPB_WS_BACKEND}" STREQUAL "")
            message(STATUS "    WebSocket:        ${IPB_WS_BACKEND_RESOLVED} (override)")
        else()
            message(STATUS "    WebSocket:        ${IPB_WS_BACKEND_RESOLVED}")
        endif()
    endif()

    if(NOT "${IPB_JSON_BACKEND}" STREQUAL "")
        message(STATUS "    JSON:             ${IPB_JSON_BACKEND_RESOLVED} (override)")
    else()
        message(STATUS "    JSON:             ${IPB_JSON_BACKEND_RESOLVED}")
    endif()

    if(IPB_ENABLE_SPARKPLUG)
        if(NOT "${IPB_PROTOBUF_BACKEND}" STREQUAL "")
            message(STATUS "    Protobuf:         ${IPB_PROTOBUF_BACKEND_RESOLVED} (override)")
        else()
            message(STATUS "    Protobuf:         ${IPB_PROTOBUF_BACKEND_RESOLVED}")
        endif()
    endif()

    message(STATUS "----------------------------------------------------------------------")

    # Features
    format_bool(IPB_ENABLE_HTTP2_RESOLVED HTTP2_STR)
    format_bool(IPB_ENABLE_STATISTICS_RESOLVED STATS_STR)
    format_bool(IPB_ENABLE_FULL_LOGGING_RESOLVED LOG_STR)
    format_bool(IPB_ENABLE_MQTT_TLS MQTT_TLS_STR)
    format_bool(IPB_ENABLE_HTTP_TLS HTTP_TLS_STR)
    format_bool(IPB_ENABLE_WEBSOCKET WS_STR)

    message(STATUS "  Features:")
    message(STATUS "    HTTP/2:           ${HTTP2_STR}")
    message(STATUS "    Statistics:       ${STATS_STR}")
    message(STATUS "    Full Logging:     ${LOG_STR}")
    message(STATUS "    MQTT TLS:         ${MQTT_TLS_STR}")
    message(STATUS "    HTTP TLS:         ${HTTP_TLS_STR}")
    message(STATUS "    WebSocket:        ${WS_STR}")

    message(STATUS "----------------------------------------------------------------------")

    # Protocols
    format_bool(IPB_ENABLE_OPCUA OPCUA_STR)
    format_bool(IPB_ENABLE_MODBUS MODBUS_STR)
    format_bool(IPB_ENABLE_SPARKPLUG SPARKPLUG_STR)
    format_bool(IPB_ENABLE_SYSLOG SYSLOG_STR)

    message(STATUS "  Protocols:")
    message(STATUS "    OPC UA:           ${OPCUA_STR}")
    message(STATUS "    Modbus:           ${MODBUS_STR}")
    message(STATUS "    Sparkplug B:      ${SPARKPLUG_STR}")
    message(STATUS "    Syslog:           ${SYSLOG_STR}")

    message(STATUS "======================================================================")
    message(STATUS "")
endfunction()

# Simplified version for minimal output
function(ipb_print_config_minimal)
    message(STATUS "IPB: Mode=${IPB_BUILD_MODE} SSL=${IPB_SSL_BACKEND_RESOLVED} HTTP=${IPB_HTTP_BACKEND_RESOLVED}")
endfunction()
