cmake_minimum_required(VERSION 3.20)

project(rf_simulator_tests LANGUAGES CXX)

add_executable(tests test_main.cpp)

target_link_libraries(tests PRIVATE
    Catch2::Catch2WithMain
    common
)

include(CTest)
include(Catch)
catch_discover_tests(tests)