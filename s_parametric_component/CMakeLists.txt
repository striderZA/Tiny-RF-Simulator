cmake_minimum_required(VERSION 3.20)
project(s_param_engine LANGUAGES CXX)

add_library(s_param_engine STATIC
    src/s_param_engine.cpp
)

target_include_directories(s_param_engine
    PUBLIC ${PROJECT_SOURCE_DIR}/include
)

target_link_libraries(s_param_engine
    PUBLIC common simulator::node_graph_engine simulator::touchstone_parser
)
add_library(simulator::s_param_engine ALIAS s_param_engine)
