cmake_minimum_required(VERSION 3.20)

project(app LANGUAGES CXX)

# --------------------------------------------------
# App library
# --------------------------------------------------
add_library(app STATIC src/app.cpp src/inspector_panel.cpp)
add_library(simulator::app ALIAS app)

# --------------------------------------------------
# Include directories
# --------------------------------------------------
target_include_directories(app PUBLIC ${PROJECT_SOURCE_DIR}/include)

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
        simulator::splitter_engine
        simulator::s_parameter_amplifier_engine
        simulator::adc_engine
        simulator::spectrum_analyzer_engine
        simulator::spectrum_analyzer_widget
        simulator::node_graph_engine
        simulator::node_graph_widget
        common
)

target_compile_definitions(app PRIVATE PROJECT_SOURCE_DIR="${CMAKE_SOURCE_DIR}")

# IDE organization
set_target_properties(app PROPERTIES FOLDER "Application")
