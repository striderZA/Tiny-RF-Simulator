cmake_minimum_required(VERSION 3.20)
project(coax LANGUAGES CXX)

# ---- Engine (DSP only — UI lives in InspectorPanel) ----
add_library(coax_cable_engine STATIC
    src/coax_cable_engine.cpp
)

target_include_directories(coax_cable_engine
    PUBLIC ${PROJECT_SOURCE_DIR}/include
)

target_link_libraries(coax_cable_engine
    PUBLIC
        common
        simulator::node_graph_engine
        simulator::logging_core
)

add_library(simulator::coax_cable_engine ALIAS coax_cable_engine)
