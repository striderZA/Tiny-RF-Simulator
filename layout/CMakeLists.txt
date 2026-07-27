cmake_minimum_required(VERSION 3.20)

project(layout LANGUAGES CXX)

add_library(layout STATIC
    src/layout_manager.cpp
)

target_include_directories(layout PUBLIC
    ${PROJECT_SOURCE_DIR}/include
)

target_link_libraries(layout PUBLIC imgui)

add_library(simulator::layout ALIAS layout)

set_target_properties(layout PROPERTIES FOLDER "Application")
