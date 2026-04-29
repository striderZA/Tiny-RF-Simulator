cmake_minimum_required(VERSION 3.20)
project(node_graph LANGUAGES CXX)

# ---- Engine (pure data) ----
add_library(node_graph_engine STATIC
    src/node_graph_engine.cpp
)

target_include_directories(node_graph_engine
    PUBLIC ${PROJECT_SOURCE_DIR}/include
)

target_link_libraries(node_graph_engine PUBLIC common simulator::logging_core)

add_library(simulator::node_graph_engine ALIAS node_graph_engine)

# ---- UI Widget ----
add_library(node_graph_widget STATIC
    src/node_graph_widget.cpp
)

target_include_directories(node_graph_widget
    PUBLIC ${PROJECT_SOURCE_DIR}/include
)

target_link_libraries(node_graph_widget
    PUBLIC
        simulator::node_graph_engine
        imnodes
        simulator::core
)

add_library(simulator::node_graph_widget ALIAS node_graph_widget)
