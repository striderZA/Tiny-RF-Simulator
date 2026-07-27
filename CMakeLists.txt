# CMakeList.txt : Top-level CMake project file, do global configuration
# and include sub-projects here.
#
cmake_minimum_required (VERSION 3.20)

project (RfSimulator VERSION 0.11.1)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set_property(GLOBAL PROPERTY USE_FOLDERS ON)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

# Export compile_commands.json for clangd (used by .clangd config)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Include FetchContent module
include(FetchContent)

# Fetch imgui (docking branch)
FetchContent_Declare(
    imgui
    GIT_REPOSITORY https://github.com/ocornut/imgui.git
    GIT_TAG docking
)
FetchContent_MakeAvailable(imgui)

# Fetch implot
FetchContent_Declare(
    implot
    GIT_REPOSITORY https://github.com/epezent/implot.git
    GIT_TAG master
)
FetchContent_MakeAvailable(implot)

# Fetch GLFW
FetchContent_Declare(
    glfw
    GIT_REPOSITORY https://github.com/glfw/glfw.git
    GIT_TAG master
)
FetchContent_MakeAvailable(glfw)

# Fetch Catch2 for unit testing
FetchContent_Declare(
    catch2
    GIT_REPOSITORY https://github.com/catchorg/Catch2.git
    GIT_TAG v3.4.0
)
FetchContent_MakeAvailable(catch2)

# Fetch imgui_test_engine
FetchContent_Declare(
    imgui_test_engine
    GIT_REPOSITORY https://github.com/ocornut/imgui_test_engine.git
    GIT_TAG main
)
FetchContent_MakeAvailable(imgui_test_engine)

# Fetch imnodes
FetchContent_Declare(
    imnodes
    GIT_REPOSITORY https://github.com/Nelarius/imnodes.git
    GIT_TAG master
)
# Using Populate (not MakeAvailable) because imnodes ships its own CMakeLists.txt
# that defines an 'imnodes' target; we define the target manually below to ensure
# correct imgui linkage in this FetchContent-based build.
FetchContent_Populate(imnodes)

# Fetch portable-file-dialogs (single-header, for native file browser).
# Use Populate (not MakeAvailable) because PFD ships its own CMakeLists.txt
# with an older cmake_minimum_required that is incompatible with CMake 4.x.
# Since PFD is header-only, we only need the source directory for includes.
FetchContent_Declare(
    pfd
    GIT_REPOSITORY https://github.com/samhocevar/portable-file-dialogs.git
    GIT_TAG main
)
FetchContent_Populate(pfd)

# Fetch kissFFT (simple C library, no fancy build deps)
FetchContent_Declare(
    kissfft
    GIT_REPOSITORY https://github.com/mborgerding/kissfft.git
    GIT_TAG 131.1.0
)
FetchContent_Populate(kissfft)

# Fetch stb for PNG loading in icon registry (single-header, no build needed)
FetchContent_Declare(
    stb
    GIT_REPOSITORY https://github.com/nothings/stb.git
    GIT_TAG master
)
FetchContent_Populate(stb)

# Fetch nlohmann/json for serialization (header-only)
FetchContent_Declare(
    nlohmann_json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG v3.11.3
)
FetchContent_MakeAvailable(nlohmann_json)

# kissFFT builds use pkg-config in tools/; make minimal library target
add_library(kissfft STATIC
    ${kissfft_SOURCE_DIR}/kiss_fft.c
)
target_include_directories(kissfft PUBLIC ${kissfft_SOURCE_DIR})

enable_testing()

# Find OpenGL
find_package(OpenGL REQUIRED)

# Define imgui library
add_library(imgui STATIC
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_glfw.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_opengl2.cpp
)

target_include_directories(imgui PUBLIC
    ${imgui_SOURCE_DIR}
    ${imgui_SOURCE_DIR}/backends
    ${glfw_SOURCE_DIR}/include
)

target_link_libraries(imgui PUBLIC glfw OpenGL::GL)
target_compile_definitions(imgui PUBLIC IMGUI_ENABLE_TEST_ENGINE)

# Define implot library
add_library(implot STATIC
    ${implot_SOURCE_DIR}/implot.cpp
    ${implot_SOURCE_DIR}/implot_items.cpp
)

target_include_directories(implot PUBLIC
    ${implot_SOURCE_DIR}
    ${imgui_SOURCE_DIR}
)

target_link_libraries(implot PUBLIC imgui)

# Define imgui_test_engine library
add_library(imgui_test_engine STATIC
    ${imgui_test_engine_SOURCE_DIR}/imgui_test_engine/imgui_te_context.cpp
    ${imgui_test_engine_SOURCE_DIR}/imgui_test_engine/imgui_te_engine.cpp
    ${imgui_test_engine_SOURCE_DIR}/imgui_test_engine/imgui_te_coroutine.cpp
    ${imgui_test_engine_SOURCE_DIR}/imgui_test_engine/imgui_te_exporters.cpp
    ${imgui_test_engine_SOURCE_DIR}/imgui_test_engine/imgui_te_ui.cpp
    ${imgui_test_engine_SOURCE_DIR}/imgui_test_engine/imgui_te_utils.cpp
    ${imgui_test_engine_SOURCE_DIR}/imgui_test_engine/imgui_capture_tool.cpp
    ${imgui_test_engine_SOURCE_DIR}/imgui_test_engine/imgui_te_perftool.cpp
)

target_include_directories(imgui_test_engine PUBLIC
    ${imgui_test_engine_SOURCE_DIR}
    ${imgui_test_engine_SOURCE_DIR}/imgui_test_engine/thirdparty
)

target_compile_definitions(imgui_test_engine PRIVATE
    IMGUI_TEST_ENGINE_ENABLE_COROUTINE_STDTHREAD_IMPL=1
    IMGUI_TEST_ENGINE_ENABLE_CAPTURE=1
)

target_link_libraries(imgui_test_engine PUBLIC imgui PRIVATE glfw OpenGL::GL)

# Imgui was built with IMGUI_ENABLE_TEST_ENGINE, so link the test engine
# to satisfy the hook symbol references.
target_link_libraries(imgui PRIVATE imgui_test_engine)

# Define imnodes library
add_library(imnodes STATIC
    ${imnodes_SOURCE_DIR}/imnodes.cpp
)

target_include_directories(imnodes PUBLIC ${imnodes_SOURCE_DIR})
target_link_libraries(imnodes PUBLIC imgui)

# Include sub-projects.
add_subdirectory("core")
add_subdirectory("app")
add_subdirectory("signal_generator")
add_subdirectory("amplifier")
add_subdirectory("spectrum_analyzer")
add_subdirectory("common")
add_subdirectory("logging")
add_subdirectory("equalizer")
add_subdirectory ("src")
add_subdirectory("tests")
add_subdirectory("test_engine")
add_subdirectory("node_graph")
add_subdirectory("splitter")
add_subdirectory("touchstone")
add_subdirectory("mixer")
add_subdirectory("adc")
add_subdirectory("coax")
add_subdirectory("pfb_channelizer")
add_subdirectory("iq_plot")
add_subdirectory("icon_registry")
add_subdirectory("ideal_filter")
add_subdirectory("attenuator")
add_subdirectory("combiner")
# =============================================================================
# Install targets
# =============================================================================
install(TARGETS tiny-rf-simulator RUNTIME DESTINATION bin)

# Install documentation
install(FILES README.md LICENSE DESTINATION share/doc/rf-simulator)
install(DIRECTORY openwiki/ DESTINATION share/doc/rf-simulator/openwiki
  PATTERN ".git" EXCLUDE
  PATTERN ".last-update.json" EXCLUDE)

# =============================================================================
# CPack configuration
# =============================================================================
set(CPACK_PACKAGE_NAME "rf-simulator")
set(CPACK_PACKAGE_VENDOR "RF Simulator Project")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Tiny RF Simulator")
set(CPACK_PACKAGE_VERSION ${PROJECT_VERSION})
set(CPACK_PACKAGE_VERSION_MAJOR ${PROJECT_VERSION_MAJOR})
set(CPACK_PACKAGE_VERSION_MINOR ${PROJECT_VERSION_MINOR})
set(CPACK_PACKAGE_VERSION_PATCH ${PROJECT_VERSION_PATCH})
set(CPACK_PACKAGE_FILE_NAME "${CPACK_PACKAGE_NAME}-${CPACK_PACKAGE_VERSION}-${CMAKE_SYSTEM_NAME}-${CMAKE_SYSTEM_PROCESSOR}")

# Platform-specific generators
if(WIN32)
  set(CPACK_GENERATOR "ZIP")
else()
  set(CPACK_GENERATOR "TGZ")
endif()

include(CPack)
