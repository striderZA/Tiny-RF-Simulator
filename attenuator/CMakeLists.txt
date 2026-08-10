cmake_minimum_required(VERSION 3.20)
project(attenuator LANGUAGES CXX)

add_library(attenuator_engine
    src/attenuator_engine.cpp
)

target_include_directories(attenuator_engine PUBLIC ${PROJECT_SOURCE_DIR}/include)
target_link_libraries(attenuator_engine
    PUBLIC common
    PUBLIC simulator::touchstone_parser
    PUBLIC simulator::node_graph_engine
)

add_library(simulator::attenuator_engine ALIAS attenuator_engine)
