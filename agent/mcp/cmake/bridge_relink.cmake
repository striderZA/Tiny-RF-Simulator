# Moves a possibly running rf-sim-mcp.exe aside before relinking, and removes
# old copies once no process holds them. Windows only; used from
# agent/mcp/CMakeLists.txt.
#
# Inputs, passed with -D on the cmake command line:
#   RF_MODE     aside | cleanup
#   RF_BIN_DIR  directory that holds rf-sim-mcp.exe
#   RF_EXE      full path of rf-sim-mcp.exe (aside only)

if(NOT RF_MODE STREQUAL "aside" AND NOT RF_MODE STREQUAL "cleanup")
    message(FATAL_ERROR "bridge_relink: RF_MODE must be aside or cleanup, got '${RF_MODE}'")
endif()

if(RF_MODE STREQUAL "aside")
    if(NOT EXISTS "${RF_EXE}")
        return()
    endif()
    string(TIMESTAMP RF_STAMP "%Y%m%d%H%M%S" UTC)
    string(RANDOM LENGTH 6 ALPHABET 0123456789abcdef RF_RANDOM)
    set(RF_OLD "${RF_BIN_DIR}/rf-sim-mcp.${RF_STAMP}.${RF_RANDOM}.old")
    file(TO_NATIVE_PATH "${RF_OLD}" RF_OLD_NATIVE)
    file(RENAME "${RF_EXE}" "${RF_OLD}" RESULT RF_RESULT)
    if(RF_RESULT)
        message(FATAL_ERROR "rf-sim-mcp.exe is locked and could not be moved aside. Close the agent sessions that run it, then rebuild. (${RF_RESULT})")
    endif()
    message(STATUS "rf-sim-mcp: moved the previous exe aside to ${RF_OLD}")
    message(STATUS "rf-sim-mcp: if the link fails, restore with: rename \"${RF_OLD_NATIVE}\" rf-sim-mcp.exe")
else()
    file(GLOB RF_COPIES "${RF_BIN_DIR}/rf-sim-mcp.*.old")
    foreach(RF_COPY IN LISTS RF_COPIES)
        file(REMOVE "${RF_COPY}")
    endforeach()
endif()
