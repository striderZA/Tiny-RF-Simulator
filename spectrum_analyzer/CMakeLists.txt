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


# ---- Marker view model (UI-free) ----
add_library(spectrum_analyzer_markers
    STATIC
        src/spectrum_analyzer_markers.cpp
)

target_include_directories(spectrum_analyzer_markers
    PUBLIC ${PROJECT_SOURCE_DIR}/include
)

target_link_libraries(spectrum_analyzer_markers
    PUBLIC common
)

add_library(simulator::spectrum_analyzer_markers ALIAS spectrum_analyzer_markers)

# ---- Measurement source selection (UI-free) ----
add_library(spectrum_analyzer_measurement
    STATIC
        src/spectrum_analyzer_measurement.cpp
)

target_include_directories(spectrum_analyzer_measurement
    PUBLIC ${PROJECT_SOURCE_DIR}/include
)

add_library(simulator::spectrum_analyzer_measurement ALIAS spectrum_analyzer_measurement)

# ---- Peak table view state (UI-free) ----
add_library(spectrum_analyzer_peak_table
    STATIC
        src/spectrum_analyzer_peak_table.cpp
)

target_include_directories(spectrum_analyzer_peak_table
    PUBLIC ${PROJECT_SOURCE_DIR}/include
)

target_link_libraries(spectrum_analyzer_peak_table
    PUBLIC common
)

add_library(simulator::spectrum_analyzer_peak_table ALIAS spectrum_analyzer_peak_table)

# ---- Harmonic measurement view state (UI-free) ----
add_library(spectrum_analyzer_harmonics
    STATIC
        src/spectrum_analyzer_harmonics.cpp
)

target_include_directories(spectrum_analyzer_harmonics
    PUBLIC ${PROJECT_SOURCE_DIR}/include
)

add_library(simulator::spectrum_analyzer_harmonics ALIAS spectrum_analyzer_harmonics)

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
        simulator::spectrum_analyzer_harmonics
        simulator::spectrum_analyzer_markers
        simulator::spectrum_analyzer_measurement
        simulator::spectrum_analyzer_peak_table
        simulator::spectrum_analyzer_engine 
        simulator::core 
        simulator::pfb_channelizer_engine
        implot
)

add_library(simulator::spectrum_analyzer_widget ALIAS spectrum_analyzer_widget)
