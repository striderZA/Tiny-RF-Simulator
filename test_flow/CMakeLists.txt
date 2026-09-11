cmake_minimum_required(VERSION 3.20)
project(test_flow LANGUAGES CXX)

add_library(test_flow STATIC
    src/flow_metrics.cpp
    src/flow_params.cpp
    src/flow_result.cpp
    src/flow_runner.cpp
)
add_library(simulator::test_flow ALIAS test_flow)

target_include_directories(test_flow PUBLIC ${PROJECT_SOURCE_DIR}/include)

# GUI-free by contract: no simulator::app (ComponentRegistry lives there and
# links imgui/implot/imnodes), no imgui, implot, or imnodes.
target_link_libraries(test_flow
    PUBLIC
        common
        simulator::node_graph_engine
        simulator::power_meter_engine
)
set_target_properties(test_flow PROPERTIES FOLDER "Library")
