cmake_minimum_required(VERSION 3.20)

project(core LANGUAGES CXX)

# Core UI subsystem (ImGui + ImPlot + backend)
add_library(core STATIC
    src/core.cpp
)

add_library(simulator::core ALIAS core)

# --------------------------------------------------
# Include directories
# --------------------------------------------------
target_include_directories(core
    PUBLIC
        ${PROJECT_SOURCE_DIR}/include       # core public headers
    PRIVATE
        ${imgui_SOURCE_DIR}
        ${imgui_SOURCE_DIR}/backends
        ${implot_SOURCE_DIR}
        ${glfw_SOURCE_DIR}/include
)

# --------------------------------------------------
# Link required UI libraries
# --------------------------------------------------
target_link_libraries(core
    PUBLIC
        imgui
        implot
        glfw
        OpenGL::GL
)

# Optional: group UI dependencies into a folder in IDEs
set_target_properties(core PROPERTIES FOLDER "UI")
