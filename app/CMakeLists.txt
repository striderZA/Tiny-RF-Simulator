cmake_minimum_required(VERSION 3.20)

project(app LANGUAGES CXX)

# --------------------------------------------------
# App library
# --------------------------------------------------
add_library(app STATIC
    src/app.cpp
)

add_library(simulator::app ALIAS app)

# --------------------------------------------------
# Include directories
# --------------------------------------------------
target_include_directories(app
    PUBLIC
        ${PROJECT_SOURCE_DIR}/include   # app.h and other public headers
)

# --------------------------------------------------
# Link dependencies
# --------------------------------------------------
target_link_libraries(app
    PUBLIC
        simulator::core                 # UI backend (ImGui/GLFW/OpenGL hidden behind core)
        simulator::signal_generator
        simulator::spectrum_analyzer
        common
)

# IDE organization
set_target_properties(app PROPERTIES FOLDER "Application")
