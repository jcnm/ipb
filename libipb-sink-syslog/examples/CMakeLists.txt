# IPB Syslog Sink Examples
cmake_minimum_required(VERSION 3.16)

# Basic syslog example
add_executable(basic_syslog_example
    basic_syslog_example.cpp
)

target_link_libraries(basic_syslog_example
    ipb-common
    ipb-sink-syslog
)

# Install examples
install(TARGETS basic_syslog_example
    RUNTIME DESTINATION bin/examples
)

