cmake_minimum_required(VERSION 3.20)

project(rf_simulator_ui_tests LANGUAGES CXX)

add_executable(test_ui
    main.cpp
    ui_tests.cpp
)

target_link_libraries(test_ui PRIVATE
    imgui_test_engine
    simulator::app
    simulator::core
    common
)

include(CTest)
add_test(NAME test_ui COMMAND test_ui)
