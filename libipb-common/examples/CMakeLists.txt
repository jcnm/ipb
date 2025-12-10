# IPB Common Examples
cmake_minimum_required(VERSION 3.16)

# Basic example
add_executable(basic_data_point_example
    basic_data_point_example.cpp
)

target_link_libraries(basic_data_point_example
    ipb-common
)

# Install examples
install(TARGETS basic_data_point_example
    RUNTIME DESTINATION bin/examples
)

