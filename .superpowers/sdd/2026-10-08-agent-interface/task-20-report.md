# Task 20 implementation report

## Change

- Split project state conversion into `ProjectSerializer::toJson()` and `fromJson()` with `ProjectJsonOptions`; project-file `save()`/`load()` retain their existing filename, atomic-write, size-cap, parse-recovery, and app-orchestration roles.
- `save()` calls `toJson({project_dir, true})`, adds the filename-derived name, and writes `root.dump(2)` through the existing sibling-temp/rename path. `load()` keeps the existing size and parse checks, then calls `fromJson(root, {project_dir, true}, path)`.
- S-parameter relativization and resolution now run only when `sparam_root` is present. Window flags are serialized/restored only when `window_state` is enabled; the existing top-level section-shape guard remains.
- Added the inline test-only serializer and epoch accessors. Updated `app/AGENTS.md`; the existing `tests/AGENTS.md` entry accurately describes the new standalone test.
- The first focused CTest run exposed that `RfSimulatorApp` seeds a demo circuit in its constructor, while two new cases expected an empty project. Added `app.newProject()` to those two test setups without changing assertions or production behavior.

## Verification

### Test-first RED (recorded before implementation)

The controller ran the following before implementation; the task brief records the expected compile failure on the missing `testProjectSerializer()` and `testProjectEpoch()` accessors. The full compiler output was not forwarded to this worker.

```text
cmake --build build --target test_project_json
```

### Formatting

The repository wrapper was attempted but could not resolve the Git root in its shell context:

```text
bash scripts/format.sh app/include/project_serializer.h app/include/app.h app/src/project_serializer.cpp tests/test_project_json.cpp
fatal: not a git repository: (null)
scripts/format.sh: line 12: cd: null directory
```

Fallback using the installed `clang-format version 18.1.8` succeeded (exit 0, no diagnostics):

```text
clang-format -i app/include/project_serializer.h app/include/app.h app/src/project_serializer.cpp tests/test_project_json.cpp && clang-format --dry-run --Werror app/include/project_serializer.h app/include/app.h app/src/project_serializer.cpp tests/test_project_json.cpp
```

### Focused build

Final focused build passed:

```text
cmake --build build --target test_project_json
[1/2] Building CXX object tests/CMakeFiles/test_project_json.dir/test_project_json.cpp.obj
[2/2] Linking CXX executable bin\test_project_json.exe
```

### Initial focused CTest failure and correction

Command:

```text
ctest --test-dir build -R '^(test_project_json|test_issue113_project_load|test_path_containment|test_receiver_requirements_project|test_rf_switch_project)$' --output-on-failure
```

Complete output:

```text
Test project E:/Jaco/Projects/rf-sim/rf-simulator/.worktrees/feat-agent-interface/build
    Start 231: test_rf_switch_project
1/5 Test #231: test_rf_switch_project ...............   Passed    0.26 sec
    Start 246: test_path_containment
2/5 Test #246: test_path_containment ................   Passed    1.38 sec
    Start 259: test_issue113_project_load
3/5 Test #259: test_issue113_project_load ...........   Passed    0.82 sec
    Start 260: test_project_json
4/5 Test #260: test_project_json ....................***Failed    0.49 sec
Randomness seeded to: 402552920

~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
test_project_json.exe is a Catch2 v3.4.0 host application.
Run with -? for options

-------------------------------------------------------------------------------
Checkpoint snapshots keep S-parameter paths verbatim
-------------------------------------------------------------------------------
E:/Jaco/Projects/rf-sim/rf-simulator/.worktrees/feat-agent-interface/tests/test_project_json.cpp:59
...............................................................................

E:/Jaco/Projects/rf-sim/rf-simulator/.worktrees/feat-agent-interface/tests/test_project_json.cpp:71: FAILED:
  REQUIRE( snapshot.at("components").size() == 1 )
with expansion:
  3 == 1

-------------------------------------------------------------------------------
fromJson replaces the project like a load
-------------------------------------------------------------------------------
E:/Jaco/Projects/rf-sim/rf-simulator/.worktrees/feat-agent-interface/tests/test_project_json.cpp:102
...............................................................................

E:/Jaco/Projects/rf-sim/rf-simulator/.worktrees/feat-agent-interface/tests/test_project_json.cpp:124: FAILED:
  REQUIRE( restored.size() == 1 )
with expansion:
  2 == 1

===============================================================================
test cases:  4 |  2 passed | 2 failed
assertions: 13 | 11 passed | 2 failed

    Start 272: test_receiver_requirements_project
5/5 Test #272: test_receiver_requirements_project ...   Passed    2.39 sec

80% tests passed, 1 tests failed out of 5

Total Test time (real) =   5.54 sec

The following tests FAILED:
	260 - test_project_json (Failed)
```

Root cause was the constructor-seeded demo generator/amplifier. The two affected tests now explicitly start from `newProject()`; no assertions were changed.

### Final focused CTest

Passed all 5 selected tests:

```text
ctest --test-dir build -R '^(test_project_json|test_issue113_project_load|test_path_containment|test_receiver_requirements_project|test_rf_switch_project)$' --output-on-failure
Test project E:/Jaco/Projects/rf-sim/rf-simulator/.worktrees/feat-agent-interface/build
    Start 231: test_rf_switch_project
1/5 Test #231: test_rf_switch_project ...............   Passed    0.09 sec
    Start 246: test_path_containment
2/5 Test #246: test_path_containment ................   Passed    1.35 sec
    Start 259: test_issue113_project_load
3/5 Test #259: test_issue113_project_load ...........   Passed    0.66 sec
    Start 260: test_project_json
4/5 Test #260: test_project_json ....................   Passed    0.61 sec
    Start 272: test_receiver_requirements_project
5/5 Test #272: test_receiver_requirements_project ...   Passed    2.65 sec

100% tests passed, 0 tests failed out of 5

Total Test time (real) =   5.39 sec
```

### Main test binary

Passed:

```text
build/bin/tests --skip-benchmarks
Randomness seeded to: 410540464
===============================================================================
All tests passed (66416 assertions in 227 test cases)
```

### Diff whitespace check

Both staged-file checks returned exit 0 with no whitespace diagnostics:

```text
git diff --cached --check
git diff --check HEAD
```

No full CTest suite or unrelated tests were run.