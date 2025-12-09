project(logging)

# Backend
add_library(logging_core STATIC
    src/logging_core.cpp
)
target_include_directories(logging_core PUBLIC ${PROJECT_SOURCE_DIR}/include)
add_library(simulator::logging_core ALIAS logging_core)

# UI widget
add_library(logging_widget STATIC
    src/logging_widget.cpp
)

target_include_directories(logging_widget 
    PUBLIC ${PROJECT_SOURCE_DIR}/include
)

target_link_libraries(logging_widget 
    PUBLIC 
    simulator::logging_core 
    imgui
)

add_library(simulator::logging_widget ALIAS logging_widget)
