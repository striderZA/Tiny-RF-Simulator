cmake_minimum_required(VERSION 3.20)
project(signal_generator LANGUAGES CXX)

# ---- Engine (DSP) ----
add_library(signal_generator_engine STATIC
    src/signal_generator_engine.cpp
)

target_include_directories(signal_generator_engine
    PUBLIC ${PROJECT_SOURCE_DIR}/include
)

target_link_libraries(signal_generator_engine PUBLIC common)

add_library(simulator::signal_generator_engine ALIAS signal_generator_engine)

# ---- UI Widget ----
add_library(signal_generator_widget STATIC
    src/signal_generator_widget.cpp
)

target_include_directories(signal_generator_widget
    PUBLIC ${PROJECT_SOURCE_DIR}/include
)

target_link_libraries(signal_generator_widget
    PUBLIC 
        simulator::signal_generator_engine 
        simulator::core
        simulator::logging_widget
)

add_library(simulator::signal_generator_widget ALIAS signal_generator_widget)
