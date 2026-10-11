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
if(WIN32)
    set(RFSIM_MCP_RELINK_SCRIPT "${CMAKE_CURRENT_SOURCE_DIR}/cmake/bridge_relink.cmake")
    add_custom_command(TARGET rf-sim-mcp PRE_LINK
        COMMAND ${CMAKE_COMMAND} -DRF_MODE=aside
                "-DRF_BIN_DIR=$<TARGET_FILE_DIR:rf-sim-mcp>"
                "-DRF_EXE=$<TARGET_FILE:rf-sim-mcp>"
                -P "${RFSIM_MCP_RELINK_SCRIPT}"
        VERBATIM)
    add_custom_command(TARGET rf-sim-mcp POST_BUILD
        COMMAND ${CMAKE_COMMAND} -DRF_MODE=cleanup
                "-DRF_BIN_DIR=$<TARGET_FILE_DIR:rf-sim-mcp>"
                -P "${RFSIM_MCP_RELINK_SCRIPT}"
        VERBATIM)
endif()
target_link_libraries(rf-sim-mcp PRIVATE simulator::mcp_adapter)
target_compile_definitions(rf-sim-mcp PRIVATE RFSIM_APP_VERSION="${RFSIM_APP_VERSION}")
install(TARGETS rf-sim-mcp RUNTIME DESTINATION bin)
