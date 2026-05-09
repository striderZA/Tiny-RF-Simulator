
cmake_minimum_required(VERSION 3.20)
project(adc LANGUAGES CXX)

# ---- Engine (DSP) ----
add_library(adc_engine STATIC
    src/adc_engine.cpp
)

target_include_directories(adc_engine
    PUBLIC ${PROJECT_SOURCE_DIR}/include
)

target_link_libraries(adc_engine PUBLIC common simulator::node_graph_engine nlohmann_json::nlohmann_json)
add_library(simulator::adc_engine ALIAS adc_engine)
