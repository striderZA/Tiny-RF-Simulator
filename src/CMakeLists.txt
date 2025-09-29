add_executable(rf-simulator
    main.cpp
    rf-simulator-app.cpp
)

target_link_libraries(rf-simulator
    PRIVATE
        rf-simulator-core
)