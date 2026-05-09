
cmake_minimum_required(VERSION 3.20)
project(mixer LANGUAGES CXX)

# ---- Engine (DSP) ----
add_library(mixer_engine STATIC
    src/mixer_engine.cpp
)

target_include_directories(mixer_engine
    PUBLIC ${PROJECT_SOURCE_DIR}/include
)

target_link_libraries(mixer_engine PUBLIC common simulator::node_graph_engine nlohmann_json::nlohmann_json)
add_library(simulator::mixer_engine ALIAS mixer_engine)
