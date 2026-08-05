cmake_minimum_required(VERSION 3.20)

project(rf_simulator_tests LANGUAGES CXX)

set(TEST_SOURCES
    test_main.cpp
    test_node_graph_engine.cpp
    test_touchstone.cpp
    test_adc.cpp
    test_pfb.cpp
    test_bench_dsp.cpp
    test_bench_groups.cpp
    test_ideal_filter.cpp
    test_component_registry.cpp
    test_component_library.cpp
    test_coax_cable_presets.cpp
    test_coax_cable_engine.cpp
    test_group.cpp
    test_project_file.cpp
    test_amplifier_sparam.cpp
    test_ideal_filter_sparam.cpp
    test_equalizer.cpp
    test_iq_plot.cpp
    test_nonlinear_p1db.cpp
    test_amplifier_p1db.cpp
    test_layout_manager.cpp
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
    simulator::adc_engine
    simulator::node_graph_engine
    simulator::coax_cable_engine
    simulator::equalizer_engine
    simulator::attenuator_engine
    simulator::combiner_engine
    simulator::pfb_channelizer_engine
    simulator::iq_plot_widget
    simulator::app
    simulator::layout
)
target_compile_definitions(tests PRIVATE PROJECT_SOURCE_DIR="${CMAKE_SOURCE_DIR}")

include(CTest)
add_test(NAME tests_all COMMAND tests WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
add_executable(test_attenuator test_attenuator.cpp)
target_link_libraries(test_attenuator PRIVATE
    simulator::attenuator_engine
    simulator::node_graph_engine
    Catch2::Catch2WithMain
)
target_compile_definitions(test_attenuator PRIVATE PROJECT_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
add_test(NAME test_attenuator COMMAND test_attenuator WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})

add_executable(test_combiner test_combiner.cpp)
target_link_libraries(test_combiner PRIVATE
    simulator::combiner_engine
    simulator::node_graph_engine
    Catch2::Catch2WithMain
)
target_compile_definitions(test_combiner PRIVATE PROJECT_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
add_test(NAME test_combiner COMMAND test_combiner WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})

# Standalone executable for all new tests added by the component-library authoring UI
# plan (docs/superpowers/plans/2026-07-28-component-library-authoring-ui.md). Kept out
# of the main `tests` binary because this MinGW-w64 toolchain silently drops any
# TEST_CASE registered beyond the ~217 already linked into tests.exe (confirmed via a
# from-scratch clean rebuild).
add_executable(test_component_authoring test_component_authoring.cpp)
target_link_libraries(test_component_authoring PRIVATE
    simulator::app
    Catch2::Catch2WithMain
)
target_compile_definitions(test_component_authoring PRIVATE PROJECT_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
add_test(NAME test_component_authoring COMMAND test_component_authoring WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})

add_executable(test_component_library_curves test_component_library_curves.cpp)
target_link_libraries(test_component_library_curves PRIVATE
    simulator::app
    Catch2::Catch2WithMain
)
target_compile_definitions(test_component_library_curves PRIVATE PROJECT_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
add_test(NAME test_component_library_curves COMMAND test_component_library_curves
         WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})

add_executable(test_amplifier_nf_curve test_amplifier_nf_curve.cpp)
target_link_libraries(test_amplifier_nf_curve PRIVATE
    simulator::amplifier_engine
    simulator::node_graph_engine
    Catch2::Catch2WithMain
)
target_compile_definitions(test_amplifier_nf_curve PRIVATE PROJECT_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
add_test(NAME test_amplifier_nf_curve COMMAND test_amplifier_nf_curve
         WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})

add_executable(test_amplifier_export test_amplifier_export.cpp)
target_link_libraries(test_amplifier_export PRIVATE
    simulator::app
    Catch2::Catch2WithMain
)
target_compile_definitions(test_amplifier_export PRIVATE PROJECT_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
add_test(NAME test_amplifier_export COMMAND test_amplifier_export
         WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})

add_executable(test_amplifier_digitizer_model test_amplifier_digitizer_model.cpp)
target_link_libraries(test_amplifier_digitizer_model PRIVATE
    simulator::app
    Catch2::Catch2WithMain
)
target_compile_definitions(test_amplifier_digitizer_model PRIVATE PROJECT_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
add_test(NAME test_amplifier_digitizer_model COMMAND test_amplifier_digitizer_model
         WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})

add_executable(test_amplifier_import_wizard test_amplifier_import_wizard.cpp)
target_link_libraries(test_amplifier_import_wizard PRIVATE
    simulator::app
    Catch2::Catch2WithMain
)
target_compile_definitions(test_amplifier_import_wizard PRIVATE PROJECT_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
add_test(NAME test_amplifier_import_wizard COMMAND test_amplifier_import_wizard
         WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})

add_executable(test_extensions test_extensions.cpp)
target_link_libraries(test_extensions PRIVATE
    simulator::app
    Catch2::Catch2WithMain
)
target_compile_definitions(test_extensions PRIVATE PROJECT_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
add_test(NAME test_extensions COMMAND test_extensions WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})

