cmake_minimum_required(VERSION 3.20)

project(rf_simulator_ui_tests LANGUAGES CXX)

add_executable(test_ui
    main.cpp
    ui_tests.cpp
    test_helpers.cpp
)

target_link_libraries(test_ui PRIVATE
    imgui_test_engine
    simulator::app
    simulator::core
    common
)

target_include_directories(test_ui PRIVATE
    ${stb_SOURCE_DIR}
)

# Normalize path to use forward slashes (avoids escape sequence issues on Windows)
file(TO_CMAKE_PATH "${CMAKE_SOURCE_DIR}" PROJECT_SOURCE_DIR_NORMALIZED)
target_compile_definitions(test_ui PRIVATE
    PROJECT_SOURCE_DIR="${PROJECT_SOURCE_DIR_NORMALIZED}"
)

include(CTest)
add_test(NAME test_ui COMMAND test_ui)
