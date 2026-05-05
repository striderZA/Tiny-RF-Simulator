cmake_minimum_required(VERSION 3.20)
project(icon_registry LANGUAGES CXX)

add_library(icon_registry STATIC
    src/icon_registry.cpp
    src/texture_loader.cpp
)

target_include_directories(icon_registry
    PUBLIC ${PROJECT_SOURCE_DIR}/include
    PRIVATE ${PROJECT_SOURCE_DIR}/src
)

target_include_directories(icon_registry
    PRIVATE ${stb_SOURCE_DIR}
)

target_link_libraries(icon_registry
    PUBLIC
        imgui
    PRIVATE
        OpenGL::GL
)

add_library(simulator::icon_registry ALIAS icon_registry)
