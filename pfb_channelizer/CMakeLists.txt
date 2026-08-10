cmake_minimum_required(VERSION 3.20)

project(pfb_channelizer_engine LANGUAGES CXX)

add_library(pfb_channelizer_engine STATIC src/pfb_channelizer_engine.cpp)
add_library(simulator::pfb_channelizer_engine ALIAS pfb_channelizer_engine)

target_include_directories(pfb_channelizer_engine PUBLIC ${PROJECT_SOURCE_DIR}/include)

target_link_libraries(pfb_channelizer_engine
    PUBLIC
        simulator::node_graph_engine
        common
)

set_target_properties(pfb_channelizer_engine PROPERTIES FOLDER "Library")

add_library(pfb_channelizer_widget STATIC src/pfb_channelizer_widget.cpp)
add_library(simulator::pfb_channelizer_widget ALIAS pfb_channelizer_widget)

target_include_directories(pfb_channelizer_widget PUBLIC ${PROJECT_SOURCE_DIR}/include)

target_link_libraries(pfb_channelizer_widget
    PUBLIC
        simulator::pfb_channelizer_engine
    PRIVATE
        imgui
)
