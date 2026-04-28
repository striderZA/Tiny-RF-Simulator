
cmake_minimum_required(VERSION 3.20)
project(amplifier LANGUAGES CXX)

# ---- Engine (DSP) ----
add_library(amplifier_engine STATIC
    src/amplifier_engine.cpp
)

target_include_directories(amplifier_engine
    PUBLIC ${PROJECT_SOURCE_DIR}/include
)

target_link_libraries(amplifier_engine PUBLIC common simulator::node_graph_engine)

add_library(simulator::amplifier_engine ALIAS amplifier_engine)

# ---- UI Widget ----
add_library(amplifier_widget STATIC
    src/amplifier_widget.cpp
)

target_include_directories(amplifier_widget
    PUBLIC ${PROJECT_SOURCE_DIR}/include
)

target_link_libraries(amplifier_widget
    PUBLIC 
        simulator::amplifier_engine 
        simulator::core
        simulator::logging_widget
)

add_library(simulator::amplifier_widget ALIAS amplifier_widget)
