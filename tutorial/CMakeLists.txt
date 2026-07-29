cmake_minimum_required(VERSION 3.20)

project(tutorial LANGUAGES CXX)

add_library(tutorial STATIC
    src/tutorial_steps.cpp
    src/tutorial_state.cpp
    src/tutorial_widget.cpp
)

target_include_directories(tutorial PUBLIC
    ${PROJECT_SOURCE_DIR}/include
)

target_link_libraries(tutorial PUBLIC imgui)

add_library(simulator::tutorial ALIAS tutorial)

set_target_properties(tutorial PROPERTIES FOLDER "Application")
