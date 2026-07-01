cmake_minimum_required(VERSION 3.20)
project(equalizer LANGUAGES CXX)

add_library(equalizer_engine STATIC
    src/equalizer_engine.cpp
)

target_include_directories(equalizer_engine
    PUBLIC ${PROJECT_SOURCE_DIR}/include
)

target_link_libraries(equalizer_engine PUBLIC common simulator::node_graph_engine)
add_library(simulator::equalizer_engine ALIAS equalizer_engine)
