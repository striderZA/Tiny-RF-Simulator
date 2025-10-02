add_executable(rf-simulator
    main.cpp
    "RfSimulatorApp.cpp"
)

target_link_libraries(rf-simulator
    PRIVATE
        rf-simulator-core
        app-log
        signal-generator
        spectrum-analyzer
)