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

# Pure-DSP engine libraries shared by the main `tests` binary and the
# standalone test_signal_domain executable.
set(TEST_ENGINE_LIBS
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
)

add_executable(tests ${TEST_SOURCES})

target_link_libraries(tests PRIVATE
    Catch2::Catch2WithMain
    ${TEST_ENGINE_LIBS}
    simulator::iq_plot_widget
    simulator::app
    simulator::layout
)
target_compile_definitions(tests PRIVATE PROJECT_SOURCE_DIR="${CMAKE_SOURCE_DIR}")

include(CTest)
include(Catch)
catch_discover_tests(tests)

# Registers a standalone test executable with the standard per-test boilerplate
# (Catch2 link, PROJECT_SOURCE_DIR compile definition, CTest registration).
function(add_standalone_test NAME)
    cmake_parse_arguments(ARG "" "" "SOURCES;LIBS" ${ARGN})
    add_executable(${NAME} ${ARG_SOURCES})
    target_link_libraries(${NAME} PRIVATE ${ARG_LIBS} Catch2::Catch2WithMain)
    target_compile_definitions(${NAME} PRIVATE PROJECT_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
    add_test(NAME ${NAME} COMMAND ${NAME} WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
endfunction()

add_standalone_test(test_attenuator
    SOURCES test_attenuator.cpp
    LIBS simulator::attenuator_engine simulator::node_graph_engine
)

add_standalone_test(test_combiner
    SOURCES test_combiner.cpp
    LIBS simulator::combiner_engine simulator::node_graph_engine
)

add_standalone_test(test_network_analyzer
    SOURCES test_network_analyzer.cpp
    LIBS simulator::network_analyzer_engine simulator::network_analyzer_widget
         simulator::signal_generator_engine simulator::attenuator_engine
         simulator::amplifier_engine simulator::mixer_engine
         simulator::splitter_engine simulator::combiner_engine
         simulator::node_graph_engine
)

# Standalone executable for all new tests added by the component-library authoring UI
# plan (docs/superpowers/plans/2026-07-28-component-library-authoring-ui.md). Kept out
# of the main `tests` binary because this MinGW-w64 toolchain silently drops any
# TEST_CASE registered beyond a ceiling in tests.exe (verified 2026-08-09: 223
# registered of ~460+ in source; earlier docs said ~217). The release.yml Windows
# job guards this floor with `tests.exe --list-tests` count >= 223.
add_standalone_test(test_component_authoring
    SOURCES test_component_authoring.cpp
    LIBS simulator::app
)

# Standalone for the same reason as test_component_authoring: TEST_CASEs appended
# to the main `tests` binary are silently dropped by the MinGW-w64 toolchain, and
# these must actually run on every CI platform (TutorialState is cross-platform,
# unlike SessionState).
add_standalone_test(test_tutorial_state
    SOURCES test_tutorial_state.cpp
    LIBS simulator::tutorial
)

add_standalone_test(test_extensions
    SOURCES test_extensions.cpp
    LIBS simulator::app
)

add_standalone_test(test_issue37_pfb_input_removal
    SOURCES test_issue37_pfb_input_removal.cpp
    LIBS simulator::app
)
add_standalone_test(test_issue70_pfb_reconnect
    SOURCES test_issue70_pfb_reconnect.cpp
    LIBS simulator::app
)

add_standalone_test(test_issue42_multi_output
    SOURCES test_issue42_multi_output.cpp
    LIBS simulator::app
)

add_standalone_test(test_component_dispatch
    SOURCES test_component_dispatch.cpp
    LIBS simulator::app
)

add_standalone_test(test_signal_domain
    SOURCES test_signal_domain.cpp
    LIBS ${TEST_ENGINE_LIBS}
)
# Standalone for the source-vs-added noise-field contract (issue #82). Kept
# out of the main `tests` binary for the same MinGW-w64 registration-ceiling
# reason as test_component_authoring above.
add_standalone_test(test_signal_generator_noise
    SOURCES test_signal_generator_noise.cpp
    LIBS simulator::signal_generator_engine simulator::node_graph_engine
)

# Standalone for the S1/S2 security-fix tests (S-param path containment at the
# project-file and library boundaries + touchstone parser size/point guards).
# Kept out of the main `tests` binary for the same MinGW-w64 registration
# ceiling reason as test_component_authoring above.
add_standalone_test(test_path_containment
    SOURCES test_path_containment.cpp
    LIBS simulator::app simulator::touchstone_parser
)

# Standalone issue #48 coverage for malformed-but-valid project and component
# library JSON. Kept outside tests.exe because MinGW silently drops registrations
# beyond its test ceiling.
add_standalone_test(test_issue48_json_loader
    SOURCES test_issue48_json_loader.cpp
    LIBS simulator::app
)
add_standalone_test(test_adc_configuration
    SOURCES test_adc_configuration.cpp
    LIBS simulator::adc_engine simulator::node_graph_engine
)
# Standalone for the same MinGW-w64 registration-ceiling reason as
# test_component_authoring: new TEST_CASEs must not join the main `tests` binary.
add_standalone_test(test_pfb_filter_design
    SOURCES test_pfb_filter_design.cpp
    LIBS simulator::pfb_channelizer_engine simulator::node_graph_engine
)
add_standalone_test(test_pfb_calculator
    SOURCES test_pfb_calculator.cpp
    LIBS simulator::app
)
# Standalone issue #79 coverage: component-library definitions are validated at
# load/upsert and rejected before they reach the insertion UI or engine factory;
# engine deserialization failures roll back; path-bearing parameters get the
# same containment as data_files entries. Kept outside tests.exe for the
# MinGW-w64 registration-ceiling reason above.
add_standalone_test(test_issue79_component_validation
    SOURCES test_issue79_component_validation.cpp
    LIBS simulator::app
)
