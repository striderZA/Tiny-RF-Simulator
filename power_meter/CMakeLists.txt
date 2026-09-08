cmake_minimum_required(VERSION 3.20)
project(power_meter LANGUAGES CXX)

add_library(power_meter_engine STATIC src/power_meter_engine.cpp)
add_library(simulator::power_meter_engine ALIAS power_meter_engine)

target_include_directories(power_meter_engine PUBLIC ${PROJECT_SOURCE_DIR}/include)
target_link_libraries(power_meter_engine PUBLIC common)

add_library(power_meter_widget STATIC src/power_meter_widget.cpp)
add_library(simulator::power_meter_widget ALIAS power_meter_widget)

target_include_directories(power_meter_widget PUBLIC ${PROJECT_SOURCE_DIR}/include)
target_link_libraries(power_meter_widget
    PUBLIC
        simulator::power_meter_engine
        simulator::core
        simulator::node_graph_engine
)
