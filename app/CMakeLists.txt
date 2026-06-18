cmake_minimum_required(VERSION 3.20)

project(app LANGUAGES CXX)

# --------------------------------------------------
# App library
# --------------------------------------------------
add_library(app STATIC src/app.cpp src/inspector_panel.cpp src/component_registry.cpp)
add_library(simulator::app ALIAS app)

# --------------------------------------------------
# Include directories
# --------------------------------------------------
target_include_directories(app PUBLIC
    ${PROJECT_SOURCE_DIR}/include
    ${pfd_SOURCE_DIR}
)

# --------------------------------------------------
# Link dependencies
# --------------------------------------------------
target_link_libraries(app
    PUBLIC
        simulator::core
        simulator::signal_generator_engine
        simulator::signal_generator_widget
        simulator::amplifier_engine
        simulator::mixer_engine
        simulator::pfb_channelizer_engine
        simulator::pfb_channelizer_widget
        simulator::iq_plot_widget
        simulator::splitter_engine
        simulator::s_parameter_amplifier_engine
        simulator::s_parameter_filter_engine
        simulator::adc_engine
        simulator::coax_cable_engine
        simulator::spectrum_analyzer_engine
        simulator::spectrum_analyzer_widget
        simulator::ideal_filter_engine
        simulator::node_graph_engine
        simulator::node_graph_widget
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
    APP_VERSION="${PROJECT_VERSION}"
    APP_GIT_HASH="${GIT_COMMIT_HASH}"
)

# IDE organization
set_target_properties(app PROPERTIES FOLDER "Application")
