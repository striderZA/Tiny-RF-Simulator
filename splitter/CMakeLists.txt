
cmake_minimum_required(VERSION 3.20)
project(splitter LANGUAGES CXX)

# ---- Engine (DSP) ----
add_library(splitter_engine STATIC
    src/splitter_engine.cpp
)

target_include_directories(splitter_engine
    PUBLIC ${PROJECT_SOURCE_DIR}/include
)

target_link_libraries(splitter_engine PUBLIC common simulator::node_graph_engine)
add_library(simulator::splitter_engine ALIAS splitter_engine)
