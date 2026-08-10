cmake_minimum_required(VERSION 3.20)
project(network_analyzer LANGUAGES CXX)

add_library(network_analyzer_engine STATIC src/network_analyzer_engine.cpp)
add_library(simulator::network_analyzer_engine ALIAS network_analyzer_engine)

target_include_directories(network_analyzer_engine PUBLIC ${PROJECT_SOURCE_DIR}/include)
target_link_libraries(network_analyzer_engine
    PUBLIC common
    PUBLIC simulator::node_graph_engine
)
set_target_properties(network_analyzer_engine PROPERTIES FOLDER "Library")
