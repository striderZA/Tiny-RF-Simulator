
cmake_minimum_required(VERSION 3.20)
project(s_parameter_amplifier LANGUAGES CXX)

# ---- Engine (DSP) ----
add_library(s_parameter_amplifier_engine STATIC
    src/s_parameter_amplifier_engine.cpp
)

target_include_directories(s_parameter_amplifier_engine
    PUBLIC ${PROJECT_SOURCE_DIR}/include
)

target_link_libraries(s_parameter_amplifier_engine
    PUBLIC common simulator::node_graph_engine simulator::touchstone_parser nlohmann_json::nlohmann_json
)
add_library(simulator::s_parameter_amplifier_engine ALIAS s_parameter_amplifier_engine)
