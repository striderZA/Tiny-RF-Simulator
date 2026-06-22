cmake_minimum_required(VERSION 3.20)

project(rf_simulator_tests LANGUAGES CXX)

set(TEST_SOURCES
    test_main.cpp
    test_node_graph_engine.cpp
    test_touchstone.cpp
    test_s_parameter_amplifier.cpp
    test_adc.cpp
    test_pfb.cpp
    test_bench_dsp.cpp
    test_bench_groups.cpp
    test_ideal_filter.cpp
    test_component_registry.cpp
    test_coax_cable_presets.cpp
    test_coax_cable_engine.cpp
    test_group.cpp
    test_project_file.cpp
)

if(WIN32)
    list(APPEND TEST_SOURCES test_session_state.cpp)
endif()

add_executable(tests ${TEST_SOURCES})

target_link_libraries(tests PRIVATE
    Catch2::Catch2WithMain
    common
    simulator::signal_generator_engine
    simulator::amplifier_engine
    simulator::ideal_filter_engine
    simulator::splitter_engine
    simulator::mixer_engine
    simulator::spectrum_analyzer_engine
    simulator::touchstone_parser
        simulator::s_param_engine
    simulator::adc_engine
    simulator::node_graph_engine
    simulator::coax_cable_engine
    simulator::pfb_channelizer_engine
    simulator::app
)
target_compile_definitions(tests PRIVATE PROJECT_SOURCE_DIR="${CMAKE_SOURCE_DIR}")

include(CTest)
include(Catch)
catch_discover_tests(tests)
