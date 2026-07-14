cmake_minimum_required(VERSION 3.20)
project(combiner LANGUAGES CXX)

# ---- Engine (DSP) ----
add_library(combiner_engine STATIC
    src/combiner_engine.cpp
)

target_include_directories(combiner_engine
    PUBLIC ${PROJECT_SOURCE_DIR}/include
)

target_link_libraries(combiner_engine PUBLIC common simulator::node_graph_engine simulator::touchstone_parser)
add_library(simulator::combiner_engine ALIAS combiner_engine)
