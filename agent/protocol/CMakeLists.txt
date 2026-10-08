add_library(agent_protocol STATIC
    src/agent_wire.cpp
    src/agent_socket.cpp
    src/agent_errors.cpp
    src/agent_token.cpp
    src/agent_catalog.cpp
    src/agent_endpoint.cpp
)
add_library(simulator::agent_protocol ALIAS agent_protocol)

target_include_directories(agent_protocol PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(agent_protocol
    PUBLIC
        nlohmann_json::nlohmann_json
)

if(WIN32)
    target_compile_definitions(agent_protocol PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    target_link_libraries(agent_protocol PRIVATE bcrypt advapi32 user32 ws2_32)
endif()

set_target_properties(agent_protocol PROPERTIES FOLDER "Library")
