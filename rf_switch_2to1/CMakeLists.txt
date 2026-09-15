cmake_minimum_required(VERSION 3.20)
project(rf_switch_2to1 LANGUAGES CXX)

# ---- Engine (DSP) ----
add_library(rf_switch_2to1_engine STATIC
    src/rf_switch_2to1_engine.cpp
)

target_include_directories(rf_switch_2to1_engine
    PUBLIC ${PROJECT_SOURCE_DIR}/include
)

target_link_libraries(rf_switch_2to1_engine PUBLIC common simulator::node_graph_engine)
add_library(simulator::rf_switch_2to1_engine ALIAS rf_switch_2to1_engine)
