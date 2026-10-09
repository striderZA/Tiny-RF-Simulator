find_package(Threads REQUIRED)

add_library(mcp_adapter STATIC
    src/mcp_session.cpp
    src/gui_link.cpp
    src/mcp_bridge.cpp
)
add_library(simulator::mcp_adapter ALIAS mcp_adapter)

target_include_directories(mcp_adapter PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(mcp_adapter
    PUBLIC
        simulator::agent_protocol
        Threads::Threads
    PRIVATE
        simulator::logging_core
)

set_target_properties(mcp_adapter PROPERTIES FOLDER "Library")
add_executable(rf-sim-mcp src/main.cpp)
target_link_libraries(rf-sim-mcp PRIVATE simulator::mcp_adapter)
target_compile_definitions(rf-sim-mcp PRIVATE RFSIM_APP_VERSION="${RFSIM_APP_VERSION}")
install(TARGETS rf-sim-mcp RUNTIME DESTINATION bin)
