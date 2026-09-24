cmake_minimum_required(VERSION 3.20)

project(rf_simulator_ui_tests LANGUAGES CXX)

add_executable(test_ui
    main.cpp
    ui_tests.cpp
    test_helpers.cpp
)

target_link_libraries(test_ui PRIVATE
    imgui_test_engine
    simulator::app
    simulator::core
    common
)

include(CTest)
add_test(NAME test_ui COMMAND test_ui)

# test_ui is the only test that drives the app's exe-relative state, and every
# other test executable in build/bin shares those paths: ui_tests.cpp saves and
# deletes <exe_dir>/layouts/UITestLayout.ini (asserted by
# layout_save_as_creates_file and layout_manage_delete_removes_file) while
# tests/test_layout_manager.cpp calls remove_all(layoutsDir()), and its tutorial
# cases assert <exe_dir>/.tutorial_completed persists (asserted by
# tutorial_completes_and_persists and tutorial_first_run_prompt_marks_completed)
# while tests/test_tutorial_state.cpp removes that same marker. Neither path is
# injectable, so under a local `ctest -jN` another test can delete a file
# test_ui is asserting on; both interferences reproduce (verified 2026-09-18).
# RUN_SERIAL stops test_ui from overlapping any other test; measured cost is
# ~2.4 s on a ~10 s `-j8` run (9.6 s unserialized, 12.1 s serialized). CI never
# sees it: release.yml excludes test_ui with `-E "Benchmark|test_ui"`, so that
# cost lands on local -jN runs only. The cheaper follow-up is to make the state
# directory injectable and drop this.
set_tests_properties(test_ui PROPERTIES RUN_SERIAL TRUE)
