cmake_minimum_required(VERSION 3.20)
project(digital_step_attenuator LANGUAGES CXX)

add_library(digital_step_attenuator_engine
    src/digital_step_attenuator_engine.cpp
)

target_include_directories(digital_step_attenuator_engine PUBLIC ${PROJECT_SOURCE_DIR}/include)
target_link_libraries(digital_step_attenuator_engine
    PUBLIC simulator::attenuator_engine
)

add_library(simulator::digital_step_attenuator_engine ALIAS digital_step_attenuator_engine)
