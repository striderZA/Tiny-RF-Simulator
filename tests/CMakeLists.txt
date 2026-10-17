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
    cmake_parse_arguments(ARG "" "" "SOURCES;LIBS;ARGS" ${ARGN})
    add_executable(${NAME} ${ARG_SOURCES})
    target_link_libraries(${NAME} PRIVATE ${ARG_LIBS} Catch2::Catch2WithMain)
    target_compile_definitions(${NAME} PRIVATE PROJECT_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
    add_test(NAME ${NAME} COMMAND ${NAME} ${ARG_ARGS} WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
endfunction()

if(WIN32)
    add_standalone_test(test_session_state_unicode
        SOURCES test_session_state_unicode.cpp
        LIBS common
    )
endif()

add_standalone_test(test_attenuator
    SOURCES test_attenuator.cpp
    LIBS simulator::attenuator_engine simulator::node_graph_engine
)

add_standalone_test(test_combiner
    SOURCES test_combiner.cpp
    LIBS simulator::combiner_engine simulator::node_graph_engine
)

# Issue #238: pins the per-process temp root that tests/test_temp_paths.h installs.
add_standalone_test(test_issue238_temp_isolation
    SOURCES test_issue238_temp_isolation.cpp
)

# Rewire pass characterization and scaling benchmark; standalone for the
# MinGW-w64 registration ceiling (see tests/AGENTS.md).
add_standalone_test(test_node_graph_rewire
    SOURCES test_node_graph_rewire.cpp
    LIBS simulator::combiner_engine simulator::node_graph_engine
)

# Legacy sparam_path fallback for the amplifier, equalizer, and ideal filter.
# Standalone for the MinGW-w64 registration ceiling (see tests/AGENTS.md).
add_standalone_test(test_sparam_legacy_key
    SOURCES test_sparam_legacy_key.cpp
    LIBS simulator::amplifier_engine simulator::equalizer_engine
         simulator::ideal_filter_engine simulator::signal_generator_engine
         simulator::node_graph_engine
)

# AMP (MathWorks) IIP3/OIP3 reader. Standalone for the MinGW-w64 registration ceiling.
add_standalone_test(test_amp_file
    SOURCES test_amp_file.cpp
    LIBS simulator::amplifier_engine
)

# Per-frequency OIP3 in the nonlinear model's third-order products. Standalone for the
# MinGW-w64 registration ceiling.
add_standalone_test(test_nonlinear_frequency_oip3
    SOURCES test_nonlinear_frequency_oip3.cpp
    LIBS common
)

# AMP intercepts reaching the amplifier's nonlinear products through update(). Standalone for
# the MinGW-w64 registration ceiling.
add_standalone_test(test_amplifier_amp_file
    SOURCES test_amplifier_amp_file.cpp
    LIBS simulator::amplifier_engine simulator::node_graph_engine
)

add_standalone_test(test_rf_switch
    SOURCES test_rf_switch.cpp
    LIBS simulator::rf_switch_engine simulator::node_graph_engine
)

# Project-file round trip for the SPDT switch: covers the registry
# project_type lookup, the serializer, and the restoration of a link on the
# switch's second output pin (T2), none of which the engine-level
# test_rf_switch case can reach. Kept outside tests.exe for the MinGW-w64
# registration-ceiling reason above.
add_standalone_test(test_rf_switch_project
    SOURCES test_rf_switch_project.cpp
    LIBS simulator::app
)

add_standalone_test(test_rf_switch_2to1
    SOURCES test_rf_switch_2to1.cpp
    LIBS simulator::rf_switch_2to1_engine simulator::node_graph_engine
)

add_standalone_test(test_network_analyzer
    SOURCES test_network_analyzer.cpp
    LIBS simulator::network_analyzer_engine simulator::network_analyzer_widget
         simulator::signal_generator_engine simulator::attenuator_engine
         simulator::amplifier_engine simulator::mixer_engine
         simulator::splitter_engine simulator::combiner_engine
         simulator::rf_switch_engine simulator::rf_switch_2to1_engine
         simulator::ideal_filter_engine
         simulator::node_graph_engine
         simulator::app
)

set_tests_properties(test_network_analyzer PROPERTIES RUN_SERIAL TRUE)
add_standalone_test(test_power_meter
    SOURCES test_power_meter.cpp
    LIBS simulator::power_meter_engine
)
add_standalone_test(test_power_meter_app
    SOURCES test_power_meter_app.cpp
    LIBS simulator::app
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
# Standalone contract test for serialized component state metadata and array-path normalization.
add_standalone_test(test_component_params
    SOURCES test_component_params.cpp
    LIBS simulator::editor_services simulator::test_flow
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

if(NOT WIN32)
    add_standalone_test(test_external_tool_launch
        SOURCES test_external_tool_launch.cpp
        LIBS simulator::app simulator::agent_protocol
    )
endif()

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

# Standalone for S-param path containment, external project RF data round trips,
# touchstone parser guards, and component-authoring filename checks. Kept outside
# tests.exe for the MinGW registration ceiling.
add_standalone_test(test_path_containment
    SOURCES test_path_containment.cpp
    LIBS simulator::app simulator::touchstone_parser
)
set_tests_properties(test_path_containment PROPERTIES RUN_SERIAL TRUE)
# Separate target keeps the newly added project-data cases below MinGW's per-binary
# Catch2 registration ceiling while retaining the existing containment suite.
add_standalone_test(test_external_project_data
    SOURCES test_path_containment.cpp
    LIBS simulator::app simulator::touchstone_parser
)
target_compile_definitions(test_external_project_data PRIVATE TEST_EXTERNAL_PROJECT_DATA_ONLY=1)
set_tests_properties(test_external_project_data PROPERTIES RUN_SERIAL TRUE)

# Standalone issue #48 coverage for malformed-but-valid project and component
# library JSON. Kept outside tests.exe because MinGW silently drops registrations
# beyond its test ceiling.
add_standalone_test(test_issue48_json_loader
    SOURCES test_issue48_json_loader.cpp
    LIBS simulator::app
)
# Issue #200 app-level swap side effects: the Network Analyzer and Power Meter pins follow the
# successor, and a PFB keeps its IQ Plot and Grid visibility. Constructs RfSimulatorApp, which
# writes the shared exe-relative app.ini, so run it serially.
add_standalone_test(test_component_swap_app
    SOURCES test_component_swap_app.cpp
    LIBS simulator::app
)
set_tests_properties(test_component_swap_app PROPERTIES RUN_SERIAL TRUE)
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
# Standalone issue #77 coverage: a failed save (unwritable target path) must
# not clear the project's dirty state or replace the current path. Kept
# outside tests.exe for the MinGW-w64 registration-ceiling reason above.
add_standalone_test(test_issue77_save_failure
    SOURCES test_issue77_save_failure.cpp
    LIBS simulator::app
)
# Standalone issue #224 regression tests: project save refuses temp-path links
# without modifying their victims; kept outside tests.exe for the MinGW ceiling.
add_standalone_test(test_issue224_project_temp_links
    SOURCES test_issue224_project_temp_links.cpp
    LIBS simulator::app
)
set_tests_properties(test_issue224_project_temp_links PROPERTIES RUN_SERIAL TRUE)

# Standalone issue #78 coverage: splitter/PFB multi-output pin API reporting
# and OUT2 link/probe project round trips. Kept outside tests.exe for the
# MinGW-w64 registration-ceiling reason above.
add_standalone_test(test_issue78_multi_output
    SOURCES test_issue78_multi_output.cpp
    LIBS simulator::app
)

# Standalone issue #80 coverage: extension-execution hardening (workspace id
# allowlist + canonical containment, per-invocation workspace isolation,
# result-file size bound, app-level containment of hand-built manifests).
# Kept outside tests.exe and out of test_extensions.cpp for the MinGW-w64
# registration-ceiling reason above.
add_standalone_test(test_issue80_extension_hardening
    SOURCES test_issue80_extension_hardening.cpp
    LIBS simulator::app
)
# Standalone issue #45 coverage: project-local external tools stay refused
# until the user records an approval in ExtensionTrustStore, and a duplicate
# extension id is reported as Shadowed instead of replacing the first
# manifest that claimed it. Kept outside tests.exe for the MinGW-w64
# registration-ceiling reason above.
add_standalone_test(test_issue45_extension_trust
    SOURCES test_issue45_extension_trust.cpp
    LIBS simulator::app
)

# Both targets construct RfSimulatorApp, which writes executable-relative
# app.ini on destruction and uses the executable-relative extension trust
# store unless a test redirects it. Keep them serialized; source-tree built-in
# discovery roots are injectable and fixture writes use each process's temp root.
# Per-invocation workspaces under <temp>/rf-sim-extension-run are not shared.
set_tests_properties(test_extensions test_issue45_extension_trust
                     PROPERTIES RUN_SERIAL TRUE)

# Standalone sampling-ratio coverage stays outside tests.exe because of the
# MinGW-w64 registration ceiling.
add_standalone_test(test_pfb_sampling_ratio
    SOURCES test_pfb_sampling_ratio.cpp
    LIBS simulator::pfb_channelizer_engine simulator::node_graph_engine
)

# Standalone issue #87 coverage: the GUI-free test-flow harness (flow file
# loading, parameter sweeps, metric capture, JSON export). Kept outside
# tests.exe for the MinGW-w64 registration-ceiling reason above.
add_standalone_test(test_issue87_flow
    SOURCES test_issue87_flow.cpp
    LIBS simulator::test_flow simulator::signal_generator_engine
         simulator::amplifier_engine simulator::attenuator_engine
         simulator::adc_engine simulator::pfb_channelizer_engine
         simulator::node_graph_engine
)

# Standalone flow-index coverage stays outside the issue #87 binary for the MinGW-w64
# registration ceiling.
add_standalone_test(test_issue130_flow_indices
    SOURCES test_issue130_flow_indices.cpp
    LIBS simulator::test_flow simulator::signal_generator_engine
)

# Standalone coverage for the spectrum-display noise jitter: the cosmetic
# noise-floor fluctuation must not move deterministic tone peaks. Kept outside
# tests.exe for the MinGW-w64 registration-ceiling reason above.
add_standalone_test(test_spectrum_jitter
    SOURCES test_spectrum_jitter.cpp
    LIBS simulator::spectrum_analyzer_engine
)

# Regression coverage for issue #113 (failed-load save-target leak, malformed
# window_state/graph_state guards, atomic save). Kept outside tests.exe for the
# MinGW-w64 registration-ceiling reason above.
add_standalone_test(test_issue113_project_load
    SOURCES test_issue113_project_load.cpp
    LIBS simulator::app
)
# Constructs RfSimulatorApp, which writes the shared exe-relative app.ini, so run it serially.
set_tests_properties(test_issue113_project_load PROPERTIES RUN_SERIAL TRUE)
# Standalone serializer JSON transformation coverage, separate from file I/O.
add_standalone_test(test_project_json
    SOURCES test_project_json.cpp
    LIBS simulator::app
)
set_tests_properties(test_project_json PROPERTIES RUN_SERIAL TRUE)

# Standalone issue #116 coverage: collapsed subcircuit blocks render from a
# cached grid position, links between two different collapsed groups are drawn
# through their boundary pins, and the editor rejects duplicate-input and cyclic
# links. Kept outside tests.exe for the MinGW-w64 registration-ceiling reason
# above.
add_standalone_test(test_issue116_collapsed_groups
    SOURCES test_issue116_collapsed_groups.cpp
    LIBS simulator::app simulator::node_graph_widget
)
# Standalone issue #117 coverage: six independent DSP numeric-correctness
# regressions. Kept outside tests.exe because of the MinGW-w64 registration
# ceiling.
add_standalone_test(test_issue117_numeric_correctness
    SOURCES test_issue117_numeric_correctness.cpp
    LIBS simulator::signal_generator_engine
         simulator::amplifier_engine
         simulator::ideal_filter_engine
         simulator::equalizer_engine
         simulator::mixer_engine
         simulator::combiner_engine
         simulator::adc_engine
         simulator::node_graph_engine
         simulator::touchstone_parser
)

# Standalone: new TEST_CASEs stay out of the main tests binary (MinGW-w64
# registration ceiling), and out of test_extensions.cpp, following
# test_issue80_extension_hardening.
add_standalone_test(test_issue130_extension_menu_labels
    SOURCES test_issue130_extension_menu_labels.cpp
    LIBS simulator::app
)

# Standalone coverage for the analyzer-owned strongest-tone SNR measurement:
# RBW sensitivity, full stored tone power on real-domain and complex-baseband
# spectra, the unavailable-input guards, and the read-only contract against the
# render caches/trace history. Kept outside tests.exe for the MinGW-w64
# registration-ceiling reason above.
add_standalone_test(test_spectrum_analyzer_snr
    SOURCES test_spectrum_analyzer_snr.cpp
    LIBS simulator::spectrum_analyzer_engine
)

# Standalone coverage for issue #239: the RBW and VBW filters pass invalid widths
# through and clamp very large width ratios before the int conversion. Kept
# outside tests.exe for the MinGW-w64 registration-ceiling reason above.
add_standalone_test(test_issue239_rbw_vbw_bounds
    SOURCES test_issue239_rbw_vbw_bounds.cpp
    LIBS simulator::spectrum_analyzer_engine
)

# Standalone app-level coverage for the node-hover SNR row: the onNodeHover
# callback carries the registry summary plus the analyzer's strongest-tone SNR
# for the node's first output, and reports "unavailable" when that output has no
# measurable tone. Kept outside tests.exe for the MinGW-w64 registration-ceiling
# reason above.
add_standalone_test(test_node_hover_snr
    SOURCES test_node_hover_snr.cpp
    LIBS simulator::app
)
# Constructs RfSimulatorApp, which writes the shared exe-relative app.ini, so run it serially.
set_tests_properties(test_node_hover_snr PROPERTIES RUN_SERIAL TRUE)

# Standalone coverage for the app-owned Test Flow runner panel: the
# all-component snapshot/restore boundary around RunFlow() (a sweep must leave
# every serialize() state, every graph link, and the dirty flag untouched), the
# ordinary-failure path, and the latched restoration failure. Kept outside
# tests.exe for the MinGW-w64 registration-ceiling reason above.
add_standalone_test(test_test_flow_widget
    SOURCES test_test_flow_widget.cpp
    LIBS simulator::app simulator::test_flow
)

# App-level tests share the exe-relative build/bin/app.ini that
# RfSimulatorApp::~RfSimulatorApp() writes, exactly like test_ui: a concurrent
# run would read-modify-write that one file from two processes.
set_tests_properties(test_test_flow_widget PROPERTIES RUN_SERIAL TRUE)

# UI-free coverage for the one-shot Test Flow boundary and the AgentServer's resumable session.
add_standalone_test(test_flow_boundary
    SOURCES test_flow_boundary.cpp
    LIBS simulator::test_flow simulator::editor_services common
)

# Standalone, UI-free behavioral coverage for component/topology ownership.
add_standalone_test(test_circuit_runtime
    SOURCES test_circuit_runtime.cpp
    LIBS simulator::circuit_runtime simulator::signal_generator_engine
         simulator::amplifier_engine simulator::adc_engine
         simulator::pfb_channelizer_engine
)

# Standalone, UI-free behavioral coverage for app-owned graph editor actions.
add_standalone_test(test_graph_editor_actions
    SOURCES test_graph_editor_actions.cpp
    LIBS simulator::app
)

# Editor command service (UI-free cases) plus app-level cases proving the test
# hooks, loader, and New share its side effects. Constructs RfSimulatorApp, so
# it shares the exe-relative app.ini and must run serially.
add_standalone_test(test_editor_commands
    SOURCES test_editor_commands.cpp
    LIBS simulator::app
)
set_tests_properties(test_editor_commands PROPERTIES RUN_SERIAL TRUE)
# Standalone, UI-free coverage for the editor-services public build boundary.
add_standalone_test(test_editor_services_boundary
    SOURCES test_editor_services_boundary.cpp
    LIBS simulator::editor_services
)
set_target_properties(test_editor_services_boundary PROPERTIES FOLDER "Library")

# Standalone, UI-free coverage for the in-place library part swap (issue #200): id and link
# preservation, port/probe/instrument refusals, group and probe remapping, and the PFB link policy.
add_standalone_test(test_component_swap
    SOURCES test_component_swap.cpp
    LIBS simulator::editor_services
)

# Standalone UI-free validator/evaluator and draft-application coverage.
add_standalone_test(test_receiver_requirements
    SOURCES test_receiver_requirements.cpp
    LIBS simulator::app
)
# Standalone app/serializer persistence coverage; kept outside tests.exe for
# the MinGW-w64 registration ceiling.
add_standalone_test(test_receiver_requirements_project
    SOURCES test_receiver_requirements_project.cpp
    LIBS simulator::app
)
set_tests_properties(test_receiver_requirements_project PROPERTIES RUN_SERIAL TRUE)
# Standalone real-engine coverage for the UI-free receiver measurement engine.
add_standalone_test(test_receiver_performance_measurements
    SOURCES test_receiver_performance_measurements.cpp
    LIBS simulator::app
    ARGS --skip-benchmarks
)

# Standalone issue #182 coverage: utils::inputDouble()/inputFrequency() show and
# keep a value outside their limits, log it once, and clamp only user edits
# (never against the edit's direction); includes the inspector amplifier-gain
# reproduction. Kept outside tests.exe for the MinGW-w64 registration ceiling.
add_standalone_test(test_issue182_bounded_inputs
    SOURCES test_issue182_bounded_inputs.cpp
    LIBS simulator::app simulator::core
)
# Standalone issue #174 compatibility contract for library field keys versus
# engine serialization keys, plus a serialize/deserialize fixed point for every
# registered engine. Kept out of tests.exe for the MinGW registration ceiling.
add_standalone_test(test_issue174_parameter_keys
    SOURCES test_issue174_parameter_keys.cpp
    LIBS simulator::app
)

# Standalone, UI-free agent protocol framing and error-shape contract tests.
add_standalone_test(test_agent_protocol
    SOURCES test_agent_protocol.cpp
    LIBS simulator::agent_protocol
)

# Standalone, UI-free behavioral contract tests for the AgentApi read surface.
add_standalone_test(test_agent_api
    SOURCES test_agent_api.cpp
    LIBS simulator::agent_api
)
target_include_directories(test_agent_api PRIVATE ${CMAKE_SOURCE_DIR}/agent/api/src)
# Standalone, UI-free circuit_edit move and editor-space position contracts. Kept apart from
# test_agent_api.cpp, whose added cases were not registered on the MinGW-w64 build.
add_standalone_test(test_agent_api_moves
    SOURCES test_agent_api_moves.cpp
    LIBS simulator::agent_api
)
target_include_directories(test_agent_api_moves PRIVATE ${CMAKE_SOURCE_DIR}/agent/api/src)
# Standalone real-loopback coverage for AgentServer transport, authentication,
# UI-thread pumping, modal parking, endpoint ownership, and shutdown.
add_standalone_test(test_agent_server
    SOURCES test_agent_server.cpp
    LIBS simulator::agent_api
)

# Standalone, UI- and simulator-free coverage for both MCP protocol eras.
add_standalone_test(test_mcp_bridge
    SOURCES test_mcp_bridge.cpp
    LIBS simulator::mcp_adapter
)
# Runs the built bridge as a separate process against an in-process UI-free AgentServer, covering
# packaged discovery, a stateful edit/read round trip, and reconnection after server restart.
add_standalone_test(test_agent_packaged_acceptance
    SOURCES test_agent_packaged_acceptance.cpp
    LIBS simulator::agent_api
)
target_include_directories(test_agent_packaged_acceptance PRIVATE ${CMAKE_SOURCE_DIR}/agent/api/src)
target_compile_definitions(test_agent_packaged_acceptance PRIVATE
    RFSIM_MCP_EXECUTABLE="$<TARGET_FILE:rf-sim-mcp>"
)
add_dependencies(test_agent_packaged_acceptance rf-sim-mcp)


# Standalone app-level agent host coverage: checkpoint lifecycle and trimming
# (newest 20 kept), component auto-layout placement, explicit positions, and
# activity logging (newest 50 kept, "Agent:" prefix). Constructs RfSimulatorApp
# through AgentFixture (ImGuiFixture), so it shares the exe-relative app.ini
# and must run serially.
add_standalone_test(test_agent_app
    SOURCES test_agent_app.cpp
    LIBS simulator::app
)
set_tests_properties(test_agent_app PROPERTIES RUN_SERIAL TRUE)
# Standalone app-level regression for agent-visible node positions: GUI-added and duplicated
# nodes before their first draw, pan-aware round trips, agent moves, and collapsed-group persistence.
add_standalone_test(test_agent_position_read
    SOURCES test_agent_position_read.cpp
    LIBS simulator::app
)
set_tests_properties(test_agent_position_read PROPERTIES RUN_SERIAL TRUE)

# Standalone, UI-free contracts for the project_save and project_load tools: path and overwrite
# rules, the unsaved-changes and epoch guards, the test-flow latch, and host outcome mapping.
add_standalone_test(test_agent_api_project
    SOURCES test_agent_api_project.cpp
    LIBS simulator::agent_api
)
target_include_directories(test_agent_api_project PRIVATE ${CMAKE_SOURCE_DIR}/agent/api/src)

# Standalone app-level project_save and project_load round trips through RfSimulatorApp: real
# .rfsim files, the GUI save and load paths, checkpoint clearing, and reset-on-failure.
add_standalone_test(test_agent_project
    SOURCES test_agent_project.cpp
    LIBS simulator::app
)
set_tests_properties(test_agent_project PROPERTIES RUN_SERIAL TRUE)

# Standalone tests for the active circuit project's Git branch, commit, and working-tree status.
add_standalone_test(test_git_status
    SOURCES test_git_status.cpp
    LIBS simulator::app
)

# The fake Git process gives the status timeout regression a deterministic slow child.
add_executable(test_git_status_hanging_git test_git_status_hanging_git.cpp)
add_dependencies(test_git_status test_git_status_hanging_git)
target_compile_definitions(test_git_status PRIVATE
    GIT_STATUS_HANG_HELPER="$<TARGET_FILE:test_git_status_hanging_git>"
)
