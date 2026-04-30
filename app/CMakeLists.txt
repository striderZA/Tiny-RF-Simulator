cmake_minimum_required(VERSION 3.20)

project(app LANGUAGES CXX)

# --------------------------------------------------
# App library
# --------------------------------------------------
add_library(app STATIC src/app.cpp)
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
        simulator::amplifier_widget
        simulator::splitter_engine
        simulator::splitter_widget
        simulator::mixer_engine
        simulator::mixer_widget
        simulator::spectrum_analyzer_engine
        simulator::spectrum_analyzer_widget
        simulator::node_graph_engine
        simulator::node_graph_widget
        common
)

# IDE organization
set_target_properties(app PROPERTIES FOLDER "Application")
