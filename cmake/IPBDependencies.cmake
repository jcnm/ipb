# IPBDependencies.cmake - Library Selection Based on Mode and User Overrides
# This file finds and configures dependencies based on the selected backends

#=============================================================================
# SSL/TLS Backend Selection
#=============================================================================

function(ipb_find_ssl)
    set(IPB_SSL_FOUND FALSE PARENT_SCOPE)
    set(IPB_SSL_LIBRARIES "" PARENT_SCOPE)
    set(IPB_SSL_INCLUDE_DIRS "" PARENT_SCOPE)

    if(IPB_SSL_BACKEND_RESOLVED STREQUAL "openssl")
        find_package(OpenSSL QUIET)
        if(OpenSSL_FOUND)
            set(IPB_SSL_FOUND TRUE PARENT_SCOPE)
            set(IPB_SSL_LIBRARIES OpenSSL::SSL OpenSSL::Crypto PARENT_SCOPE)
            set(IPB_SSL_INCLUDE_DIRS ${OPENSSL_INCLUDE_DIR} PARENT_SCOPE)
            set(IPB_SSL_VERSION ${OPENSSL_VERSION} PARENT_SCOPE)
            message(STATUS "SSL Backend: OpenSSL ${OPENSSL_VERSION}")
        else()
            message(WARNING "OpenSSL not found, trying alternatives...")
        endif()

    elseif(IPB_SSL_BACKEND_RESOLVED STREQUAL "mbedtls")
        find_package(MbedTLS QUIET)
        if(NOT MbedTLS_FOUND)
            # Try pkg-config
            pkg_check_modules(MBEDTLS QUIET mbedtls mbedcrypto mbedx509)
        endif()
        if(MbedTLS_FOUND OR MBEDTLS_FOUND)
            set(IPB_SSL_FOUND TRUE PARENT_SCOPE)
            if(TARGET MbedTLS::mbedtls)
                set(IPB_SSL_LIBRARIES MbedTLS::mbedtls MbedTLS::mbedcrypto MbedTLS::mbedx509 PARENT_SCOPE)
            else()
                set(IPB_SSL_LIBRARIES ${MBEDTLS_LIBRARIES} PARENT_SCOPE)
                set(IPB_SSL_INCLUDE_DIRS ${MBEDTLS_INCLUDE_DIRS} PARENT_SCOPE)
            endif()
            message(STATUS "SSL Backend: mbedTLS")
        else()
            message(WARNING "mbedTLS not found")
        endif()

    elseif(IPB_SSL_BACKEND_RESOLVED STREQUAL "wolfssl")
        pkg_check_modules(WOLFSSL QUIET wolfssl)
        if(WOLFSSL_FOUND)
            set(IPB_SSL_FOUND TRUE PARENT_SCOPE)
            set(IPB_SSL_LIBRARIES ${WOLFSSL_LIBRARIES} PARENT_SCOPE)
            set(IPB_SSL_INCLUDE_DIRS ${WOLFSSL_INCLUDE_DIRS} PARENT_SCOPE)
            message(STATUS "SSL Backend: wolfSSL")
        else()
            message(WARNING "wolfSSL not found")
        endif()

    elseif(IPB_SSL_BACKEND_RESOLVED STREQUAL "none")
        set(IPB_SSL_FOUND TRUE PARENT_SCOPE)
        message(STATUS "SSL Backend: None (disabled)")
    endif()
endfunction()

#=============================================================================
# HTTP Backend Selection
#=============================================================================

function(ipb_find_http)
    set(IPB_HTTP_FOUND FALSE PARENT_SCOPE)
    set(IPB_HTTP_LIBRARIES "" PARENT_SCOPE)
    set(IPB_HTTP_INCLUDE_DIRS "" PARENT_SCOPE)

    if(IPB_HTTP_BACKEND_RESOLVED STREQUAL "curl")
        find_package(CURL QUIET)
        if(CURL_FOUND)
            set(IPB_HTTP_FOUND TRUE PARENT_SCOPE)
            set(IPB_HTTP_LIBRARIES CURL::libcurl PARENT_SCOPE)
            set(IPB_HTTP_INCLUDE_DIRS ${CURL_INCLUDE_DIRS} PARENT_SCOPE)
            set(IPB_HTTP_VERSION ${CURL_VERSION_STRING} PARENT_SCOPE)
            message(STATUS "HTTP Backend: libcurl ${CURL_VERSION_STRING}")
        else()
            message(WARNING "libcurl not found")
        endif()

    elseif(IPB_HTTP_BACKEND_RESOLVED STREQUAL "cpphttplib")
        # cpp-httplib is header-only, just check if it exists
        find_path(CPPHTTPLIB_INCLUDE_DIR httplib.h
            PATHS /usr/include /usr/local/include
            PATH_SUFFIXES cpp-httplib
        )
        if(CPPHTTPLIB_INCLUDE_DIR)
            set(IPB_HTTP_FOUND TRUE PARENT_SCOPE)
            set(IPB_HTTP_INCLUDE_DIRS ${CPPHTTPLIB_INCLUDE_DIR} PARENT_SCOPE)
            message(STATUS "HTTP Backend: cpp-httplib (header-only)")
        else()
            # We can use a bundled version or fetch it
            message(STATUS "HTTP Backend: cpp-httplib (will use bundled/fetched)")
            set(IPB_HTTP_FOUND TRUE PARENT_SCOPE)
            set(IPB_HTTP_USE_BUNDLED TRUE PARENT_SCOPE)
        endif()

    elseif(IPB_HTTP_BACKEND_RESOLVED STREQUAL "lwip")
        # lwIP is typically used in embedded, check if available
        find_path(LWIP_INCLUDE_DIR lwip/tcp.h
            PATHS /usr/include /usr/local/include
        )
        if(LWIP_INCLUDE_DIR)
            set(IPB_HTTP_FOUND TRUE PARENT_SCOPE)
            set(IPB_HTTP_INCLUDE_DIRS ${LWIP_INCLUDE_DIR} PARENT_SCOPE)
            message(STATUS "HTTP Backend: lwIP")
        else()
            message(STATUS "HTTP Backend: lwIP (minimal implementation)")
            set(IPB_HTTP_FOUND TRUE PARENT_SCOPE)
            set(IPB_HTTP_USE_MINIMAL TRUE PARENT_SCOPE)
        endif()
    endif()
endfunction()

#=============================================================================
# MQTT Backend Selection
#=============================================================================

function(ipb_find_mqtt)
    set(IPB_MQTT_FOUND FALSE PARENT_SCOPE)
    set(IPB_MQTT_LIBRARIES "" PARENT_SCOPE)
    set(IPB_MQTT_INCLUDE_DIRS "" PARENT_SCOPE)

    if(IPB_MQTT_BACKEND_RESOLVED STREQUAL "paho-cpp")
        find_library(PAHO_MQTT_CPP_LIB paho-mqttpp3)
        find_library(PAHO_MQTT_C_LIB paho-mqtt3as)
        find_path(PAHO_MQTT_CPP_INCLUDE mqtt/async_client.h)

        if(PAHO_MQTT_CPP_LIB AND PAHO_MQTT_C_LIB)
            set(IPB_MQTT_FOUND TRUE PARENT_SCOPE)
            set(IPB_MQTT_LIBRARIES ${PAHO_MQTT_CPP_LIB} ${PAHO_MQTT_C_LIB} PARENT_SCOPE)
            set(IPB_MQTT_INCLUDE_DIRS ${PAHO_MQTT_CPP_INCLUDE} PARENT_SCOPE)
            message(STATUS "MQTT Backend: Paho MQTT C++")
        else()
            message(WARNING "Paho MQTT C++ not found")
        endif()

    elseif(IPB_MQTT_BACKEND_RESOLVED STREQUAL "paho-c")
        find_library(PAHO_MQTT_C_LIB paho-mqtt3as)
        find_path(PAHO_MQTT_C_INCLUDE MQTTAsync.h)

        if(PAHO_MQTT_C_LIB)
            set(IPB_MQTT_FOUND TRUE PARENT_SCOPE)
            set(IPB_MQTT_LIBRARIES ${PAHO_MQTT_C_LIB} PARENT_SCOPE)
            set(IPB_MQTT_INCLUDE_DIRS ${PAHO_MQTT_C_INCLUDE} PARENT_SCOPE)
            message(STATUS "MQTT Backend: Paho MQTT C")
        else()
            message(WARNING "Paho MQTT C not found")
        endif()

    elseif(IPB_MQTT_BACKEND_RESOLVED STREQUAL "mqtt-c")
        # MQTT-C is typically bundled for embedded
        message(STATUS "MQTT Backend: MQTT-C (minimal, will use bundled)")
        set(IPB_MQTT_FOUND TRUE PARENT_SCOPE)
        set(IPB_MQTT_USE_BUNDLED TRUE PARENT_SCOPE)
    endif()
endfunction()

#=============================================================================
# WebSocket Backend Selection
#=============================================================================

function(ipb_find_websocket)
    if(NOT IPB_ENABLE_WEBSOCKET)
        set(IPB_WS_FOUND FALSE PARENT_SCOPE)
        return()
    endif()

    set(IPB_WS_FOUND FALSE PARENT_SCOPE)
    set(IPB_WS_LIBRARIES "" PARENT_SCOPE)
    set(IPB_WS_INCLUDE_DIRS "" PARENT_SCOPE)

    if(IPB_WS_BACKEND_RESOLVED STREQUAL "beast")
        find_package(Boost COMPONENTS system QUIET)
        if(Boost_FOUND)
            set(IPB_WS_FOUND TRUE PARENT_SCOPE)
            set(IPB_WS_LIBRARIES Boost::system PARENT_SCOPE)
            set(IPB_WS_INCLUDE_DIRS ${Boost_INCLUDE_DIRS} PARENT_SCOPE)
            message(STATUS "WebSocket Backend: Boost.Beast")
        else()
            message(WARNING "Boost not found for Beast WebSocket")
        endif()

    elseif(IPB_WS_BACKEND_RESOLVED STREQUAL "ixwebsocket")
        find_library(IXWEBSOCKET_LIB ixwebsocket)
        find_path(IXWEBSOCKET_INCLUDE ixwebsocket/IXWebSocket.h)

        if(IXWEBSOCKET_LIB)
            set(IPB_WS_FOUND TRUE PARENT_SCOPE)
            set(IPB_WS_LIBRARIES ${IXWEBSOCKET_LIB} PARENT_SCOPE)
            set(IPB_WS_INCLUDE_DIRS ${IXWEBSOCKET_INCLUDE} PARENT_SCOPE)
            message(STATUS "WebSocket Backend: IXWebSocket")
        else()
            message(STATUS "WebSocket Backend: IXWebSocket (will use bundled/fetched)")
            set(IPB_WS_FOUND TRUE PARENT_SCOPE)
            set(IPB_WS_USE_BUNDLED TRUE PARENT_SCOPE)
        endif()

    elseif(IPB_WS_BACKEND_RESOLVED STREQUAL "lwip")
        message(STATUS "WebSocket Backend: lwIP (minimal implementation)")
        set(IPB_WS_FOUND TRUE PARENT_SCOPE)
        set(IPB_WS_USE_MINIMAL TRUE PARENT_SCOPE)
    endif()
endfunction()

#=============================================================================
# JSON Backend Selection
#=============================================================================

function(ipb_find_json)
    set(IPB_JSON_FOUND FALSE PARENT_SCOPE)
    set(IPB_JSON_LIBRARIES "" PARENT_SCOPE)
    set(IPB_JSON_INCLUDE_DIRS "" PARENT_SCOPE)

    if(IPB_JSON_BACKEND_RESOLVED STREQUAL "jsoncpp")
        find_package(jsoncpp QUIET)
        if(jsoncpp_FOUND OR TARGET jsoncpp_lib)
            set(IPB_JSON_FOUND TRUE PARENT_SCOPE)
            if(TARGET jsoncpp_lib)
                set(IPB_JSON_LIBRARIES jsoncpp_lib PARENT_SCOPE)
            elseif(TARGET JsonCpp::JsonCpp)
                set(IPB_JSON_LIBRARIES JsonCpp::JsonCpp PARENT_SCOPE)
            endif()
            message(STATUS "JSON Backend: jsoncpp")
        else()
            pkg_check_modules(JSONCPP QUIET jsoncpp)
            if(JSONCPP_FOUND)
                set(IPB_JSON_FOUND TRUE PARENT_SCOPE)
                set(IPB_JSON_LIBRARIES ${JSONCPP_LIBRARIES} PARENT_SCOPE)
                set(IPB_JSON_INCLUDE_DIRS ${JSONCPP_INCLUDE_DIRS} PARENT_SCOPE)
                message(STATUS "JSON Backend: jsoncpp (pkg-config)")
            endif()
        endif()

    elseif(IPB_JSON_BACKEND_RESOLVED STREQUAL "nlohmann")
        find_package(nlohmann_json QUIET)
        if(nlohmann_json_FOUND)
            set(IPB_JSON_FOUND TRUE PARENT_SCOPE)
            set(IPB_JSON_LIBRARIES nlohmann_json::nlohmann_json PARENT_SCOPE)
            message(STATUS "JSON Backend: nlohmann/json (header-only)")
        else()
            # Header-only, can be fetched
            message(STATUS "JSON Backend: nlohmann/json (will use bundled/fetched)")
            set(IPB_JSON_FOUND TRUE PARENT_SCOPE)
            set(IPB_JSON_USE_BUNDLED TRUE PARENT_SCOPE)
        endif()

    elseif(IPB_JSON_BACKEND_RESOLVED STREQUAL "cjson")
        find_library(CJSON_LIB cjson)
        find_path(CJSON_INCLUDE cJSON.h)

        if(CJSON_LIB)
            set(IPB_JSON_FOUND TRUE PARENT_SCOPE)
            set(IPB_JSON_LIBRARIES ${CJSON_LIB} PARENT_SCOPE)
            set(IPB_JSON_INCLUDE_DIRS ${CJSON_INCLUDE} PARENT_SCOPE)
            message(STATUS "JSON Backend: cJSON")
        else()
            message(STATUS "JSON Backend: cJSON (will use bundled)")
            set(IPB_JSON_FOUND TRUE PARENT_SCOPE)
            set(IPB_JSON_USE_BUNDLED TRUE PARENT_SCOPE)
        endif()

    elseif(IPB_JSON_BACKEND_RESOLVED STREQUAL "jsmn")
        # jsmn is header-only, minimal
        message(STATUS "JSON Backend: jsmn (minimal, header-only)")
        set(IPB_JSON_FOUND TRUE PARENT_SCOPE)
        set(IPB_JSON_USE_BUNDLED TRUE PARENT_SCOPE)
    endif()
endfunction()

#=============================================================================
# Protobuf Backend Selection
#=============================================================================

function(ipb_find_protobuf)
    if(NOT IPB_ENABLE_SPARKPLUG)
        set(IPB_PROTOBUF_FOUND FALSE PARENT_SCOPE)
        return()
    endif()

    set(IPB_PROTOBUF_FOUND FALSE PARENT_SCOPE)
    set(IPB_PROTOBUF_LIBRARIES "" PARENT_SCOPE)
    set(IPB_PROTOBUF_INCLUDE_DIRS "" PARENT_SCOPE)

    if(IPB_PROTOBUF_BACKEND_RESOLVED STREQUAL "protobuf")
        find_package(Protobuf QUIET)
        if(Protobuf_FOUND)
            set(IPB_PROTOBUF_FOUND TRUE PARENT_SCOPE)
            set(IPB_PROTOBUF_LIBRARIES protobuf::libprotobuf PARENT_SCOPE)
            set(IPB_PROTOBUF_INCLUDE_DIRS ${Protobuf_INCLUDE_DIRS} PARENT_SCOPE)
            message(STATUS "Protobuf Backend: Google Protobuf ${Protobuf_VERSION}")
        else()
            message(WARNING "Protobuf not found")
        endif()

    elseif(IPB_PROTOBUF_BACKEND_RESOLVED STREQUAL "protobuf-lite")
        find_package(Protobuf QUIET)
        if(Protobuf_FOUND AND TARGET protobuf::libprotobuf-lite)
            set(IPB_PROTOBUF_FOUND TRUE PARENT_SCOPE)
            set(IPB_PROTOBUF_LIBRARIES protobuf::libprotobuf-lite PARENT_SCOPE)
            set(IPB_PROTOBUF_INCLUDE_DIRS ${Protobuf_INCLUDE_DIRS} PARENT_SCOPE)
            message(STATUS "Protobuf Backend: Google Protobuf Lite")
        else()
            message(WARNING "Protobuf Lite not found")
        endif()

    elseif(IPB_PROTOBUF_BACKEND_RESOLVED STREQUAL "nanopb")
        find_path(NANOPB_INCLUDE pb.h
            PATHS /usr/include /usr/local/include
            PATH_SUFFIXES nanopb
        )
        if(NANOPB_INCLUDE)
            set(IPB_PROTOBUF_FOUND TRUE PARENT_SCOPE)
            set(IPB_PROTOBUF_INCLUDE_DIRS ${NANOPB_INCLUDE} PARENT_SCOPE)
            message(STATUS "Protobuf Backend: nanopb")
        else()
            message(STATUS "Protobuf Backend: nanopb (will use bundled)")
            set(IPB_PROTOBUF_FOUND TRUE PARENT_SCOPE)
            set(IPB_PROTOBUF_USE_BUNDLED TRUE PARENT_SCOPE)
        endif()
    endif()
endfunction()

#=============================================================================
# Master Find Function
#=============================================================================

function(ipb_find_all_dependencies)
    ipb_find_ssl()
    ipb_find_http()
    ipb_find_mqtt()
    ipb_find_websocket()
    ipb_find_json()
    ipb_find_protobuf()
endfunction()
