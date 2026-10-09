find_package(Threads REQUIRED)

add_library(agent_api STATIC
    src/agent_api.cpp
    src/agent_args.cpp
    src/tool_circuit_read.cpp
    src/tool_library.cpp
    src/agent_links.cpp
    src/agent_placement.cpp
    src/tool_circuit_edit.cpp
    src/tool_measure.cpp
)
add_library(simulator::agent_api ALIAS agent_api)

target_include_directories(agent_api
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(agent_api
    PUBLIC
        simulator::agent_protocol
        simulator::editor_services
        simulator::network_analyzer_engine
        simulator::spectrum_analyzer_engine
        simulator::power_meter_engine
        simulator::test_flow
        Threads::Threads
)
set_target_properties(agent_api PROPERTIES FOLDER "Library")
