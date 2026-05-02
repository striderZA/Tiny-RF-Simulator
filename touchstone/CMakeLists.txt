
cmake_minimum_required(VERSION 3.20)
project(touchstone LANGUAGES CXX)

add_library(touchstone_parser STATIC
    src/touchstone_parser.cpp
    src/s_parameter_data.cpp
)

target_include_directories(touchstone_parser
    PUBLIC ${PROJECT_SOURCE_DIR}/include
)

target_link_libraries(touchstone_parser
    PUBLIC common
)

add_library(simulator::touchstone_parser ALIAS touchstone_parser)
