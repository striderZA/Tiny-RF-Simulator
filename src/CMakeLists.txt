cmake_minimum_required(VERSION 3.20)

project(main LANGUAGES CXX)

add_executable(main
    main.cpp
)

target_link_libraries(main
    PRIVATE
        simulator::app     # app orchestrates everything
)

# Optional: organize inside IDE
set_target_properties(main PROPERTIES FOLDER "Executable")
