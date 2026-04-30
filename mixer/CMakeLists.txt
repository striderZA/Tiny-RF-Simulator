
cmake_minimum_required(VERSION 3.20)
project(mixer LANGUAGES CXX)

# ---- Engine (DSP) ----
add_library(mixer_engine STATIC
    src/mixer_engine.cpp
)

target_include_directories(mixer_engine
    PUBLIC ${PROJECT_SOURCE_DIR}/include
)

target_link_libraries(mixer_engine PUBLIC common simulator::node_graph_engine)

add_library(simulator::mixer_engine ALIAS mixer_engine)

# ---- UI Widget ----
add_library(mixer_widget STATIC
    src/mixer_widget.cpp
)

target_include_directories(mixer_widget
    PUBLIC ${PROJECT_SOURCE_DIR}/include
)

target_link_libraries(mixer_widget
    PUBLIC
        simulator::mixer_engine
        simulator::core
        simulator::logging_widget
)

add_library(simulator::mixer_widget ALIAS mixer_widget)
