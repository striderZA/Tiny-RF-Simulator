cmake_minimum_required(VERSION 3.20)

project(rf_simulator LANGUAGES CXX)

add_executable(tiny-rf-simulator
    main.cpp
)

target_link_libraries(tiny-rf-simulator
    PRIVATE
        simulator::app     # app orchestrates everything
)

# Optional: organize inside IDE
set_target_properties(tiny-rf-simulator PROPERTIES FOLDER "Executable")
