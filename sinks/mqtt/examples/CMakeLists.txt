# IPB MQTT Sink Examples
cmake_minimum_required(VERSION 3.16)

# Find MQTT dependencies
# First try CMake config (preferred for Windows/vcpkg)
find_package(PahoMqttCpp QUIET CONFIG)

if(PahoMqttCpp_FOUND)
    set(MQTT_EXAMPLE_LIBS PahoMqttCpp::paho-mqttpp3)
    set(MQTT_FOUND TRUE)
else()
    # Fallback to manual library search
    find_library(PAHO_MQTT_CPP_LIB paho-mqttpp3)
    find_library(PAHO_MQTT_C_LIB paho-mqtt3as)
    if(PAHO_MQTT_CPP_LIB AND PAHO_MQTT_C_LIB)
        set(MQTT_EXAMPLE_LIBS ${PAHO_MQTT_CPP_LIB} ${PAHO_MQTT_C_LIB})
        set(MQTT_FOUND TRUE)
    endif()
endif()

if(MQTT_FOUND)
    # Basic MQTT example
    add_executable(basic_mqtt_example
        basic_mqtt_example.cpp
    )

    target_link_libraries(basic_mqtt_example
        ipb-common
        ipb-sink-mqtt
        ${MQTT_EXAMPLE_LIBS}
    )

    # Install examples
    install(TARGETS basic_mqtt_example
        RUNTIME DESTINATION bin/examples
    )

    message(STATUS "MQTT sink examples enabled")
else()
    message(WARNING "MQTT libraries not found. MQTT sink examples disabled.")
endif()

