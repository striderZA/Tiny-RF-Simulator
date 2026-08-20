# Final Fix Report — feat/device-generator-cpp-extension

Commit: e099dab `fix(device_generator): guard empty/mismatched data and result-write failures`

## Findings Addressed

### 1. P1 — Data-less parameters reach generate_data_file and index an empty YMatrix (UB)
**Files:**
- `device_generator/src/params_io.cpp` — `load_params` now builds each `DeviceParameter` in a local and only adds it to the collection when it has non-empty data. Single mode: `parse_xy` runs, but a param with empty `x` is skipped entirely. Sets mode: only sets with non-empty `x` are kept; a param whose sets are all empty is not added. The existing "no parameter has data (fill x/y first)" error is preserved (triggered when the collection ends up empty). Mirrors the reference `_build_parameters`.
- `device_generator/src/data_file_writer.cpp` — `generate_data_file` now guards, after finding `ydata` for a `var_key` inside the data-row loop: `if (ydata.size() < axis.size()) throw std::invalid_argument("parameter '<name>' has fewer Y rows than the axis");`. This is Python IndexError parity and makes the build action skip the offending file per-file instead of emitting garbage bytes / UB. `ydata.size() > axis.size()` is still allowed (Python ignores extras).

### 2. P2 — my_interpolation reads front() of an empty vector
**File:** `device_generator/src/math.cpp` — `my_interpolation` now throws `std::invalid_argument("my_interpolation: empty X array")` before the `x_ay.size() < 2` early-return guard (Python ValueError parity; defense-in-depth behind fix 1a).

### 3. P2 — Tool returns 0 when result.json cannot be written
**File:** `device_generator/tool/main.cpp` — after `create_directories`, `if (!out || ec) return 4;` and after writing `if (!out.good()) return 4;`. A successful external-tool run now requires a writable result.json.

## Regression Tests Added (tests/test_device_generator.cpp)

1. `load_params skips data-less parameters` — Gain filled + s21 `{"x":[],"y":[]}` loads with `names == {"Gain"}`; sets mode keeps only non-empty sets and drops all-empty params; all-empty input still yields the "no parameter has data" error.
2. `build action drops data-less params from output` — build on Gain filled + s21 empty builds 1 file whose content has no `S21(Mag,dB)` header column.
3. `build action skips file on cross-param Y/axis row mismatch` — Gain 2 rows + s21 1 row: built 0, skipped 1, message contains "fewer Y rows", input file not moved to processed/.
4. `my_interpolation rejects empty arrays` — throws `std::invalid_argument`.

## Verification

Covering tests (all pass):

```
$ cmake --build build --target test_device_generator device_generator_tool
[1/8] Building CXX object device_generator/CMakeFiles/device_generator.dir/src/data_file_writer.cpp.obj
[2/8] Building CXX object device_generator/CMakeFiles/device_generator.dir/src/math.cpp.obj
[3/8] Building CXX object device_generator/CMakeFiles/device_generator_tool.dir/tool/main.cpp.obj
[4/8] Building CXX object device_generator/CMakeFiles/device_generator.dir/src/params_io.cpp.obj
[5/8] Linking CXX static library lib\libdevice_generator.a
[6/8] Linking CXX executable E:\Jaco\Projects\rf-sim\rf-simulator\extensions\device-generator\bin\device_generator_tool.exe
[7/8] Building CXX object tests/CMakeFiles/test_device_generator.dir/test_device_generator.cpp.obj
[8/8] Linking CXX executable bin\test_device_generator.exe

$ build/bin/test_device_generator.exe "[device_generator]"
Filters: [device_generator]
Randomness seeded to: 2120880797
===============================================================================
All tests passed (203 assertions in 51 test cases)

$ build/bin/test_extensions.exe "[device-generator]"
Filters: [device-generator]
Randomness seeded to: 1408844250
===============================================================================
All tests passed (17 assertions in 2 test cases)
```

- Golden fixtures unchanged: `git diff --stat -- tests/fixtures` is empty.
- All edited files formatted with clang-format 18.1.8 (`git diff --check` clean).
- main.cpp exit-4 path is covered by code review only (unwritable-path is hard to unit-test on Windows); no test added, per instructions.

---

# Final Fix Report — second pass (post re-review)

Commit: (see git log; message `fix(device_generator): reject empty y-rows in params loader`)

## Finding Addressed

### P2 — parse_xy accepts empty y-rows (residual UB)
**File:** `device_generator/src/params_io.cpp` — `parse_xy` accepted a parameter with non-empty `x` but empty y-rows (`{"x": [1e9, 2e9], "y": [[], []]}`): `x.size() == y.size()` and zero-column rows are column-consistent, so the parameter loaded, and consumers then indexed `row[0]` on an empty row → UB in `convert_lin_f_params`, `y_col0` (amp_f_pd/mixer), and the mixer row loop (debug assert abort / release garbage bytes with exit 0). Fix: in the nested-array branch, after the `!row.is_array()` check, empty rows are now rejected with `reason = context + ": parameter '<name>' y rows must not be empty arrays"` and `return false`. Mirrors the existing row validation; the tool's own template never produces this (it emits flat `x:[]/y:[]` which the data-less skip already drops) — closes the residual UB edge.

## Regression Test Added (tests/test_device_generator.cpp)

- `load_params rejects empty y-rows without throwing` — `"Gain": {"x": [1e9, 2e9], "y": [[], []]}` returns an error containing `"y rows"` (no throw).

## Verification

Covering tests (all pass):

```
$ cmake --build build --target test_device_generator
[1/4] Building CXX object device_generator/CMakeFiles/device_generator.dir/src/params_io.cpp.obj
[2/4] Linking CXX static library lib\libdevice_generator.a
[3/4] Building CXX object tests/CMakeFiles/test_device_generator.dir/test_device_generator.cpp.obj
[4/4] Linking CXX executable bin\test_device_generator.exe

$ cmake --build build --target device_generator_tool   # e2e tool links the changed lib
[1/1] Linking CXX executable E:\Jaco\Projects\rf-sim\rf-simulator\extensions\device-generator\bin\device_generator_tool.exe

$ build/bin/test_device_generator.exe "[device_generator]"
Filters: [device_generator]
Randomness seeded to: 2768870260
===============================================================================
All tests passed (204 assertions in 52 test cases)

$ build/bin/test_extensions.exe "[device-generator]"
Filters: [device-generator]
Randomness seeded to: 2362621587
===============================================================================
All tests passed (17 assertions in 2 test cases)
```

- Golden fixtures unchanged: `git diff --stat -- tests/fixtures` is empty.
- Edited files formatted with clang-format 18.1.8 (hermes venv binary; the PATH `clang-format` is an NPM v15 wrapper that cannot parse the repo's 18-only config keys).

---

# Final Fix Report — project loader P1 rollback + probe mapping (issue #48)

Branch: fix/issue-48-json-loader-hardening (commit: see git log; message `fix(project_loader): roll back failed components and resolve probes via saved-index mapping`)

## Findings Addressed

### P1.1 — Component created/registered before nested deserialize/metadata restore throws stays counted and linked
**File:** `app/src/project_serializer.cpp` — the per-component try block created the engine via `desc->create(...)` (which registers it in `ComponentRegistry`, registers its node in the ViewManager, and adds the graph node) before `comp->deserialize(params)` (and, for PFBs, `m_pfb_views.addFor(...)`, plus the `pos`/`part_number` metadata restoration) ran. Any throw from those steps was caught, but only pushed -1 into `new_node_ids`; the partially created component was left in `ComponentRegistry`/`NodeGraph`/PFB views, so a malformed record remained counted and linkable.
**Fix:** `desc`/`comp` now live in the try scope's enclosing block so the catch can roll back. The catch calls `m_components.remove(comp->graphNodeId())` (unregisters the node, removes the graph node, drops it from the type index and the compact view) and, for PFBs, `m_pfb_views.rebuild(m_components, m_state)` so any view state created for the failed engine is cleared while valid sibling PFB views are recreated. `new_node_ids.push_back(...)` moved to the end of the try (after every throwing step), so exactly one entry is pushed per record (the real node id on success, -1 from the catch on rollback) — no double-push/index misalignment. Valid sibling loading is preserved (the existing `continue`-based skips and the loop structure are unchanged).

### P1.2 — Probe restoration resolves saved component index against the compact registry
**File:** `app/src/project_serializer.cpp` — the `probe_pins` pass used `m_components.all()[comp_idx]` with the file's component index. A record skipped earlier (pushed -1) shifted every later saved index onto the wrong compact slot (misattaching the probe) and could drop valid probes whose saved index exceeded the compacted registry size.
**Fix:** the probe pass now resolves exactly like the links/network-analyzer passes: bounds-check `comp_idx` against `new_node_ids.size()`, read `node_id = new_node_ids[comp_idx]`, skip negative (skipped/malformed record) mappings, then `m_components.find(node_id)` with a null guard. A probe whose saved record was skipped is dropped; a valid probe after a skipped record restores to the correct component's pin.

## Regression Tests Added (tests/test_issue48_json_loader.cpp)

1. `Project loader rolls back component whose nested params throw` — `Amplifier` with `"gain_dB": "not-a-number"` between a SignalGenerator and a valid Amplifier. Asserts `componentCount() == 2` (a leftover would count 3), the valid link `0→2` still resolves through the preserved saved-index mapping, and the generator probe hits the generator's output pin. Fails on pre-fix code (`3 == 2`).
2. `Project loader resolves probes through skipped components` — first record `{"type": 42}` is skipped, then SignalGenerator (index 1) and Amplifier (index 2) load; probes `comp:1` and `comp:2` must restore to the generator's and amplifier's output pins respectively. Fails on pre-fix code (`probes.size() 1 == 2` — misattached and dropped).

Both were verified to FAIL against the reverted (pre-fix) serializer and PASS with the fix.

## Verification

```
$ cmake --build build --target test_issue48_json_loader
[1/4] Building CXX object tests/CMakeFiles/test_issue48_json_loader.dir/test_issue48_json_loader.cpp.obj
[2/4] Building CXX object app/CMakeFiles/app.dir/src/project_serializer.cpp.obj
[3/4] Linking CXX static library lib\libapp.a
[4/4] Linking CXX executable bin\test_issue48_json_loader.exe

$ ./build/bin/test_issue48_json_loader.exe
Randomness seeded to: 3452968360
===============================================================================
All tests passed (67 assertions in 10 test cases)

$ ./build/bin/tests.exe "[project_file]"
All tests passed (114 assertions in 16 test cases)

$ ./build/bin/tests.exe "[library]"
All tests passed (68 assertions in 15 test cases)

$ ./build/bin/test_path_containment.exe
All tests passed (29 assertions in 5 test cases)
```

- No unrelated changes: `git diff --stat` touches only `app/src/project_serializer.cpp` and `tests/test_issue48_json_loader.cpp`.
