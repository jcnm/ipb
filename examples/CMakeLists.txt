# IPB Examples
cmake_minimum_required(VERSION 3.16)

# Include directories
include_directories(
    ${CMAKE_SOURCE_DIR}/core/common/include
    ${CMAKE_SOURCE_DIR}/core/router/include
    ${CMAKE_SOURCE_DIR}/sinks/console/include
    ${CMAKE_SOURCE_DIR}/sinks/syslog/include
    ${CMAKE_SOURCE_DIR}/sinks/mqtt/include
)

# Find dependencies
# Find jsoncpp (avoid conflicts)
if(NOT TARGET jsoncpp_lib AND NOT TARGET JsonCpp::JsonCpp)
    find_package(jsoncpp QUIET)
    if(NOT jsoncpp_FOUND)
        pkg_check_modules(JSONCPP REQUIRED jsoncpp)
    endif()
endif()
find_package(Threads REQUIRED)

# Mock Data Flow Test Example
add_executable(mock_data_flow_test mock_data_flow_test.cpp)

target_link_libraries(mock_data_flow_test
    ipb-common
    ipb-router
    ipb-sink-console
    ipb-sink-syslog
    ipb-sink-mqtt
    jsoncpp_lib
    Threads::Threads
)

target_compile_options(mock_data_flow_test PRIVATE
    -Wall -Wextra -Wpedantic
    -O2
)

# Complete Industrial Setup Example
add_executable(complete_industrial_setup complete_industrial_setup.cpp)

target_link_libraries(complete_industrial_setup
    ipb-common
    ipb-router
    ipb-sink-console
    ipb-sink-syslog
    ipb-sink-mqtt
    jsoncpp_lib
    Threads::Threads
)

target_compile_options(complete_industrial_setup PRIVATE
    -Wall -Wextra -Wpedantic
    -O2
)

# Install examples
install(TARGETS mock_data_flow_test complete_industrial_setup
    RUNTIME DESTINATION bin/examples
)

install(FILES gateway-config.yaml
    DESTINATION share/ipb/examples
)

