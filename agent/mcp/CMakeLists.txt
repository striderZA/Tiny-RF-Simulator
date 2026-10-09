find_package(Threads REQUIRED)

add_library(mcp_adapter STATIC
    src/mcp_session.cpp
    src/gui_link.cpp
)
add_library(simulator::mcp_adapter ALIAS mcp_adapter)

target_include_directories(mcp_adapter PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(mcp_adapter
    PUBLIC
        simulator::agent_protocol
        Threads::Threads
)

set_target_properties(mcp_adapter PROPERTIES FOLDER "Library")
