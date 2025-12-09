cmake_minimum_required(VERSION 3.20)
project(spectrum_analyzer LANGUAGES CXX)

# ---- Engine (DSP) ----
add_library(spectrum_analyzer_engine 
    STATIC
        src/spectrum_analyzer_engine.cpp
)

target_include_directories(spectrum_analyzer_engine
    PUBLIC 
        ${PROJECT_SOURCE_DIR}/include
)

target_link_libraries(spectrum_analyzer_engine 
    PUBLIC 
        common
        simulator::logging_core
)

add_library(simulator::spectrum_analyzer_engine ALIAS spectrum_analyzer_engine)

# ---- UI Widget ----
add_library(spectrum_analyzer_widget 
    STATIC
        src/spectrum_analyzer_widget.cpp
)

target_include_directories(spectrum_analyzer_widget
    PUBLIC ${PROJECT_SOURCE_DIR}/include
)

target_link_libraries(spectrum_analyzer_widget
    PUBLIC 
        simulator::spectrum_analyzer_engine 
        simulator::core 
        implot
)

add_library(simulator::spectrum_analyzer_widget ALIAS spectrum_analyzer_widget)
