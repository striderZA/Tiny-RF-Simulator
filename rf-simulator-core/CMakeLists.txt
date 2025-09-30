add_library(rf-simulator-core STATIC
    rf-simulator-core.cpp
)

target_include_directories(rf-simulator-core
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(rf-simulator-core
    PUBLIC
        imgui
        implot
        glfw
        OpenGL::GL
        app-log
)