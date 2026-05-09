cmake_minimum_required(VERSION 3.20)

project(rf_simulator_tests LANGUAGES CXX)

add_executable(tests test_main.cpp test_node_graph_engine.cpp test_touchstone.cpp test_s_parameter_amplifier.cpp test_adc.cpp test_pfb.cpp test_bench_dsp.cpp test_session_state.cpp test_project_file.cpp)

target_link_libraries(tests PRIVATE
    Catch2::Catch2WithMain
    common
    simulator::app
    simulator::signal_generator_engine
    simulator::amplifier_engine
    simulator::splitter_engine
    simulator::mixer_engine
    simulator::spectrum_analyzer_engine
    simulator::touchstone_parser
        simulator::s_parameter_amplifier_engine
        simulator::s_parameter_filter_engine
    simulator::adc_engine
    simulator::node_graph_engine
    simulator::pfb_channelizer_engine
)

target_compile_definitions(tests PRIVATE PROJECT_SOURCE_DIR="${CMAKE_SOURCE_DIR}")

include(CTest)
include(Catch)
catch_discover_tests(tests)