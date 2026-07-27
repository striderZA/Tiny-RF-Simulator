cmake_minimum_required(VERSION 3.20)

project(help_widget LANGUAGES CXX)

add_library(help_widget STATIC
    src/help_widget.cpp
)

target_include_directories(help_widget PUBLIC
    ${PROJECT_SOURCE_DIR}/include
)

target_link_libraries(help_widget PUBLIC imgui)

add_library(simulator::help_widget ALIAS help_widget)

set_target_properties(help_widget PROPERTIES FOLDER "Application")
