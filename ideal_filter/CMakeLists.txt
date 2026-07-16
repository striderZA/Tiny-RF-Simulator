cmake_minimum_required(VERSION 3.20)
project(ideal_filter LANGUAGES CXX)

add_library(ideal_filter_engine STATIC
    src/ideal_filter_engine.cpp
)

target_include_directories(ideal_filter_engine
    PUBLIC ${PROJECT_SOURCE_DIR}/include
)

target_link_libraries(ideal_filter_engine PUBLIC common simulator::node_graph_engine simulator::touchstone_parser)
add_library(simulator::ideal_filter_engine ALIAS ideal_filter_engine)
