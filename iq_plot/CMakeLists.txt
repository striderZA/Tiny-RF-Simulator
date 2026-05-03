cmake_minimum_required(VERSION 3.20)

project(iq_plot_widget LANGUAGES CXX)

add_library(iq_plot_widget STATIC src/iq_plot_widget.cpp)
add_library(simulator::iq_plot_widget ALIAS iq_plot_widget)

target_include_directories(iq_plot_widget PUBLIC ${PROJECT_SOURCE_DIR}/include)

target_link_libraries(iq_plot_widget
    PUBLIC
        simulator::pfb_channelizer_engine
        simulator::core
        common
    PRIVATE
        kissfft
)

set_target_properties(iq_plot_widget PROPERTIES FOLDER "Widget")
