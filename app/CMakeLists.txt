cmake_minimum_required(VERSION 3.20)

project(app LANGUAGES CXX)

# --------------------------------------------------
# UI-independent circuit runtime library
# --------------------------------------------------
add_library(circuit_runtime STATIC
    src/circuit_runtime.cpp
    src/component_registry.cpp
)
add_library(simulator::circuit_runtime ALIAS circuit_runtime)
target_include_directories(circuit_runtime PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)
target_link_libraries(circuit_runtime
    PUBLIC
        common
        simulator::node_graph_engine
        simulator::logging_core
)

# --------------------------------------------------
# UI-free editor services library
# --------------------------------------------------
add_library(editor_services STATIC
    src/editor_commands.cpp
    src/component_params.cpp
    src/graph_editor_actions.cpp
    src/component_type_registry.cpp
    src/component_library.cpp
    src/receiver_requirements.cpp
    src/receiver_performance_measurement.cpp
    src/output_snr.cpp
)
add_library(simulator::editor_services ALIAS editor_services)
target_include_directories(editor_services PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)
target_link_libraries(editor_services
    PUBLIC
        simulator::circuit_runtime
        simulator::signal_generator_engine
        simulator::amplifier_engine
        simulator::mixer_engine
        simulator::pfb_channelizer_engine
        simulator::spectrum_analyzer_engine
        simulator::splitter_engine
        simulator::adc_engine
        simulator::coax_cable_engine
        simulator::equalizer_engine
        simulator::ideal_filter_engine
        simulator::attenuator_engine
        simulator::combiner_engine
        simulator::rf_switch_engine
        simulator::rf_switch_2to1_engine
        simulator::network_analyzer_engine
        simulator::logging_core
        common
)
set_target_properties(editor_services PROPERTIES FOLDER "Library")

# --------------------------------------------------
# App library
# --------------------------------------------------
add_library(app STATIC
    src/app.cpp
    src/app_agent_host.cpp
    src/agent_panel_widget.cpp
    src/inspector_panel.cpp
    src/library_browser_widget.cpp
    src/component_form_model.cpp
    src/component_form_widget.cpp
    src/extension_manifest.cpp
    src/extension_manager.cpp
    src/extension_trust_store.cpp
    src/external_tool_runner.cpp
    src/pfb_view_manager.cpp
    src/pfb_calculator_widget.cpp
    src/project_serializer.cpp
    src/test_flow_widget.cpp
    src/receiver_requirements_widget.cpp
)
add_library(simulator::app ALIAS app)

# --------------------------------------------------
# Include directories
# --------------------------------------------------
target_include_directories(app PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${PROJECT_SOURCE_DIR}/include
    ${pfd_SOURCE_DIR}
)

# --------------------------------------------------
# Link dependencies
# --------------------------------------------------
target_link_libraries(app
    PUBLIC
        simulator::agent_api
        simulator::circuit_runtime
        simulator::editor_services
        simulator::core
        simulator::signal_generator_widget
        simulator::pfb_channelizer_widget
        simulator::iq_plot_widget
        simulator::spectrum_analyzer_widget
        simulator::spectrum_analyzer_engine
        simulator::network_analyzer_widget
        simulator::node_graph_engine
        simulator::power_meter_engine
        simulator::power_meter_widget
        simulator::node_graph_widget
        simulator::help_widget
        simulator::layout
        simulator::tutorial
        simulator::test_flow
        common
)

# Capture git commit hash at configure time
execute_process(COMMAND git log -1 --format=%h
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    OUTPUT_VARIABLE GIT_COMMIT_HASH
    OUTPUT_STRIP_TRAILING_WHITESPACE
)

target_compile_definitions(app PRIVATE
    PROJECT_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
    APP_VERSION="${RFSIM_APP_VERSION}"
    APP_GIT_HASH="${GIT_COMMIT_HASH}"
)

# IDE organization
set_target_properties(app PROPERTIES FOLDER "Application")
