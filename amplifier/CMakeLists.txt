
cmake_minimum_required(VERSION 3.20)
project(amplifier LANGUAGES CXX)

# ---- Engine (DSP) ----
add_library(amplifier_engine STATIC
    src/amplifier_engine.cpp
)

target_include_directories(amplifier_engine
    PUBLIC ${PROJECT_SOURCE_DIR}/include
)

target_link_libraries(amplifier_engine PUBLIC common simulator::node_graph_engine nlohmann_json::nlohmann_json)
add_library(simulator::amplifier_engine ALIAS amplifier_engine)
