cmake_minimum_required(VERSION 3.20)

project(rf_simulator_tests LANGUAGES CXX)

add_executable(tests test_main.cpp test_node_graph_engine.cpp)

target_link_libraries(tests PRIVATE
    Catch2::Catch2WithMain
    common
    simulator::signal_generator_engine
    simulator::amplifier_engine
    simulator::splitter_engine
    simulator::spectrum_analyzer_engine
    simulator::node_graph_engine
)

include(CTest)
include(Catch)
catch_discover_tests(tests)