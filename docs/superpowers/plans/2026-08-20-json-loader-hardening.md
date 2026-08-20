# JSON Loader Hardening Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Prevent wrong-typed but syntactically valid project and component-library JSON from escaping load/discovery boundaries, while preserving valid sibling entries.

**Architecture:** Keep validation at the two existing boundaries: `ProjectSerializer::load()` and `ComponentLibrary::loadFile()/scan()`. Use explicit nlohmann::json type predicates before typed extraction, retain per-component saved-index mapping, skip malformed optional library data-file entries, and keep final standard-exception guards as defense in depth. Add one standalone regression executable so MinGW test registration cannot drop the new cases.

**Tech Stack:** C++20, nlohmann::json, Catch2 v3, CMake/Ninja, ImGui/ImPlot/ImNodes fixture for app-level project tests.

## Global Constraints

- Treat project and library files as untrusted input.
- Keep `{}` as a valid empty project.
- Skip malformed individual entries and preserve valid siblings.
- Do not add a generic JSON-schema dependency or unrelated parser refactoring.
- New tests must be a standalone executable because the main MinGW test binary has a registration ceiling.
- Follow the existing 4-space LLVM formatting and use `LOG_WARN`/`LOG_ERROR` for malformed input.

---

### Task 1: Add issue #48 regression executable and failing cases

**Files:**
- Create: `tests/test_issue48_json_loader.cpp`
- Modify: `tests/CMakeLists.txt:142-149`

**Interfaces:**
- Consumes: `RfSimulatorApp::loadProject()`, `ComponentLibrary::loadFile()`, `ComponentLibrary::scan()`.
- Produces: CTest target `test_issue48_json_loader` linked against `simulator::app`.

- [ ] **Step 1: Create the standalone Catch2 test source.**

Use the same ImGui/ImPlot/ImNodes fixture as `test_component_dispatch.cpp`, plus filesystem helpers that write unique temporary JSON files and remove them through RAII. Add these cases:
Define the helpers concretely so tests do not depend on the process working directory:

```cpp
namespace {

struct TempTree {
    std::filesystem::path root;
    ~TempTree() {
        std::error_code ec;
        std::filesystem::remove_all(root, ec);
    }
};

TempTree uniqueTempDirectory(std::string_view stem) {
    static std::atomic<unsigned> sequence = 0;
    TempTree tree{std::filesystem::temp_directory_path() /
                  (std::string(stem) + "_" + std::to_string(++sequence))};
    std::filesystem::create_directories(tree.root);
    return tree;
}

std::filesystem::path uniqueTempPath(std::string_view stem) {
    static std::atomic<unsigned> sequence = 0;
    return std::filesystem::temp_directory_path() /
           (std::string(stem) + "_" + std::to_string(++sequence) + ".json");
}

void writeText(const std::filesystem::path &path, std::string_view contents) {
    std::ofstream out(path);
    REQUIRE(out.good());
    out << contents;
    REQUIRE(out.good());
}

} // namespace
```

Include `<atomic>`, `<filesystem>`, `<fstream>`, `<string>`, `<string_view>`, and `<system_error>`. `TempTree` provides cleanup even when a Catch2 assertion throws; individual project files use `std::filesystem::remove(path)` at the end of their case.


```cpp
struct ImGuiFixture {
    ImGuiFixture() {
        ImGui::CreateContext();
        ImPlot::CreateContext();
        ImNodes::CreateContext();
    }
    ~ImGuiFixture() {
        ImNodes::DestroyContext();
        ImPlot::DestroyContext();
        ImGui::DestroyContext();
    }
};

TEST_CASE_METHOD(ImGuiFixture,
                 "Project loader skips malformed component and keeps valid siblings",
                 "[issue48][project]") {
    const auto path = uniqueTempPath("issue48_project");
    writeText(path, R"({
        "components": [
            {"type": "SignalGenerator", "params": {}},
            {"type": 42, "params": {}},
            {"type": "Amplifier", "params": {"gain_dB": 12.0}}
        ],
        "links": [{"from": "bad", "to": 1}, {"from": 0, "to": 2}],
        "window_state": {"log": true}
    })");

    RfSimulatorApp app;
    app.loadProject(path.string());

    REQUIRE(app.componentCount() == 2);
    REQUIRE(app.testComponents().byType<SignalGeneratorEngine>().size() == 1);
    REQUIRE(app.testComponents().byType<AmplifierEngine>().size() == 1);
}

TEST_CASE_METHOD(ImGuiFixture,
                 "Project loader rejects wrong-shaped top-level sections without throwing",
                 "[issue48][project]") {
    const auto path = uniqueTempPath("issue48_project_shape");
    writeText(path, R"({"components": 5, "links": {}, "groups": 7})");

    RfSimulatorApp app;
    app.loadProject(path.string());

    REQUIRE(app.componentCount() == 0);
}

TEST_CASE("Component library skips wrong-typed required fields", "[issue48][library]") {
    const auto path = uniqueTempPath("issue48_library_required");
    writeText(path, R"({
        "type": 7,
        "part_number": "BAD-TYPE",
        "parameters": {}
    })");

    ComponentLibrary library;
    library.loadFile(path.string());

    REQUIRE(library.all().empty());
}

TEST_CASE("Component library keeps a definition with malformed optional entries",
          "[issue48][library]") {
    const auto path = uniqueTempPath("issue48_library_optional");
    writeText(path, R"({
        "schema_version": "one",
        "type": "amplifier",
        "part_number": "GOOD-AMP",
        "manufacturer": 99,
        "description": ["wrong"],
        "parameters": {"gain_dB": 15.0},
        "data_files": [
            {"type": "s_parameters", "path": 42},
            "not-an-object",
            {"type": "s_parameters", "path": "good.s2p"}
        ]
    })");

    ComponentLibrary library;
    library.loadFile(path.string());

    const auto definitions = library.all();
    REQUIRE(definitions.size() == 1);
    REQUIRE(definitions.front()->type == "amplifier");
    REQUIRE(definitions.front()->part_number == "GOOD-AMP");
    REQUIRE(definitions.front()->data_files.size() == 1);
    REQUIRE(definitions.front()->data_files.front().path == "good.s2p");
}

TEST_CASE("Component library scan continues after malformed files", "[issue48][library]") {
    const auto root = uniqueTempDirectory("issue48_scan");
    writeText(root / "bad.json", R"({"type": [], "part_number": 4, "parameters": 9})");
    writeText(root / "good.json", R"({
        "type": "attenuator",
        "part_number": "GOOD-ATT",
        "parameters": {"attenuation_dB": 3.0}
    })");

    ComponentLibrary library;
    library.scan(root.string());

    REQUIRE(library.byType("attenuator").size() == 1);
}
```

Use the actual existing engine includes (`signal_generator_engine.h`, `amplifier_engine.h`) and a unique temporary directory under `std::filesystem::temp_directory_path()`. The test must remove all files/directories on both success and assertion failure via a scope guard.

- [ ] **Step 2: Register the standalone executable.**

Append after `test_path_containment` in `tests/CMakeLists.txt`:

```cmake
# Standalone issue #48 coverage for malformed-but-valid project and component
# library JSON. Kept outside tests.exe because MinGW silently drops registrations
# beyond its test ceiling.
add_standalone_test(test_issue48_json_loader
    SOURCES test_issue48_json_loader.cpp
    LIBS simulator::app
)
```

- [ ] **Step 3: Configure and build the new target to establish the failing baseline.**

Run:

```bash
cmake -S . -B build -G Ninja
cmake --build build --target test_issue48_json_loader
build/bin/test_issue48_json_loader.exe
```

Expected before the implementation changes: the malformed optional library case fails because the current loader discards the whole definition when an optional field or `data_files` entry has the wrong type. The existing historical project cases may already pass due to `e96af29`; retain them as regression coverage rather than weakening them.

- [ ] **Step 4: Commit the regression test separately.**

```bash
git add tests/test_issue48_json_loader.cpp tests/CMakeLists.txt
git commit -m "test: cover malformed JSON loader entries"
```

---

### Task 2: Harden component-library parsing and scanning

**Files:**
- Modify: `app/src/component_library.cpp:74-214`

**Interfaces:**
- Consumes: existing `ComponentDefinition`, `ValidationIssue`, and `ComponentLibrary` APIs.
- Produces: `loadFile()` that never propagates malformed JSON type errors and preserves valid definitions; `scan()` that continues across bad files/subtrees.

- [ ] **Step 1: Add explicit field validation before extraction.**

Inside the existing parse guard, require the root to be an object and require these fields/types:

```cpp
if (!j.is_object() || !j.contains("type") || !j["type"].is_string() ||
    !j.contains("part_number") || !j["part_number"].is_string() ||
    !j.contains("parameters") || !j["parameters"].is_object()) {
    LOG_WARN("ComponentLibrary: invalid required field types in %s", filepath.c_str());
    return;
}
```

For optional string fields, use a type check and leave the default empty string when malformed:

```cpp
const auto optionalString = [&](const char *key) {
    return j.contains(key) && j[key].is_string() ? j[key].get<std::string>() : std::string{};
};

def.manufacturer = optionalString("manufacturer");
def.description = optionalString("description");
def.notes = optionalString("notes");
```

For `schema_version`, accept only an integer and otherwise retain `1` with a warning. Keep `test_conditions` as JSON data, defaulting to an object when absent; do not call typed access on a malformed optional scalar.

- [ ] **Step 2: Isolate malformed `data_files` entries.**

Only iterate an array. For each entry, require an object with string `type` and `path`; log and continue for any invalid item:

```cpp
if (j.contains("data_files")) {
    if (!j["data_files"].is_array()) {
        LOG_WARN("ComponentLibrary: data_files is not an array in %s", filepath.c_str());
    } else {
        for (std::size_t i = 0; i < j["data_files"].size(); ++i) {
            const auto &df = j["data_files"][i];
            if (!df.is_object() || !df.contains("type") || !df["type"].is_string() ||
                !df.contains("path") || !df["path"].is_string()) {
                LOG_WARN("ComponentLibrary: skipping malformed data_files[%zu] in %s", i,
                         filepath.c_str());
                continue;
            }
            def.data_files.push_back({df["type"].get<std::string>(),
                                      df["path"].get<std::string>()});
        }
    }
}
```

Keep the final `catch (const nlohmann::json::exception&)` and add a `catch (const std::exception&)` around the whole file parse/definition build so a non-JSON parser/library exception still remains inside the file boundary.

- [ ] **Step 3: Make `scan()` non-throwing for root and traversal errors.**

Replace the throwing `fs::exists(directory)` check with the `std::error_code` overload. Keep recursive traversal in a guarded block using `skip_permission_denied`, log filesystem errors, and return to the caller without discarding definitions already loaded from prior files. `loadFile()` remains responsible for per-file malformed JSON, so later files continue to be visited.

- [ ] **Step 4: Build and run only the issue #48 executable.**

Run:

```bash
cmake --build build --target test_issue48_json_loader
build/bin/test_issue48_json_loader.exe "[library]"
```

Expected: all library cases pass, including retaining `GOOD-AMP` and discovering `GOOD-ATT` after malformed input.

- [ ] **Step 5: Commit the library hardening.**

```bash
git add app/src/component_library.cpp
git commit -m "fix: isolate malformed component library JSON"
```

---

### Task 3: Add explicit project-shape checks and final exception defense

**Files:**
- Modify: `app/src/project_serializer.cpp:296-516`

**Interfaces:**
- Consumes: existing project JSON schema and `ComponentTypeRegistry` project-type lookup.
- Produces: `ProjectSerializer::load()` that skips malformed records and returns `false` for malformed top-level sections without propagating exceptions.

- [ ] **Step 1: Validate top-level collection shapes after root-object validation.**

Before `reset()`, check optional `components`, `links`, `probe_pins`, `groups`, `network_analyzer`, `window_state`, and `graph_state` values. If an optional collection is present and non-null, require its documented object/array shape. Log the field and return `false` for an invalid top-level shape. Missing fields retain existing defaults.

- [ ] **Step 2: Validate component records before typed access.**

Within the component loop, require each record to be an object, require a string `type`, and if `params` is present require it to be an object. For malformed records, log the saved index, push `-1` into `new_node_ids`, and continue. Preserve the current behavior for absent `params` by passing the existing JSON null/default to the engine. Likewise, only read `pos.x`/`pos.y` when `pos` is an object and only read `part_number` when it is a string; malformed optional metadata should not abort valid components.

- [ ] **Step 3: Isolate malformed links, probes, network-analyzer fields, and groups.**

For each entry in the top-level arrays, require an object and validate integer/boolean fields before `value<T>()` or `get<T>()`. Log and continue on malformed entries. Keep saved-index bounds checks and `new_node_ids` mapping unchanged. This prevents one malformed link/group/probe from discarding valid components restored earlier.

- [ ] **Step 4: Broaden the final load-boundary catch.**

Keep the parse-only JSON catch around `in >> root`, but change the outer restoration catch to `catch (const std::exception &e)` so non-JSON exceptions from restoration are converted into a logged `false` result. Do not catch `...`; preserve process-level handling for non-standard fatal conditions.

- [ ] **Step 5: Build and run only the issue #48 project cases.**

Run:

```bash
cmake --build build --target test_issue48_json_loader
build/bin/test_issue48_json_loader.exe "[project]"
```

Expected: all project cases pass and malformed entries do not prevent the valid generator/amplifier siblings from loading.

- [ ] **Step 6: Commit the project hardening.**

```bash
git add app/src/project_serializer.cpp
git commit -m "fix: validate malformed project JSON"
```

---

### Task 4: Update affected DOX contracts

**Files:**
- Modify: `app/AGENTS.md:17-30`
- Modify: `tests/AGENTS.md:21-25`

**Interfaces:**
- Consumes: the final loader behavior and standalone test target from Tasks 2–3.
- Produces: durable local contracts describing malformed-input isolation and mandatory issue #48 test execution.

- [ ] **Step 1: Update the app contract.**

Add to the local contracts that `ProjectSerializer::load()` validates top-level shapes and isolates malformed component/graph records, while `ComponentLibrary::loadFile()/scan()` skip malformed definitions/files and continue discovery without propagating JSON exceptions.

- [ ] **Step 2: Update the test contract.**

Add `test_issue48_json_loader` to the standalone-test rule/examples and state that it must be run directly because it covers malformed project/library input outside the main MinGW registration ceiling.

- [ ] **Step 3: Commit the DOX update.**

```bash
git add app/AGENTS.md tests/AGENTS.md
git commit -m "docs: document JSON loader isolation contracts"
```

---

### Task 5: Run focused verification and review the branch

**Files:**
- Inspect only; no source changes expected unless verification reveals a defect.

- [ ] **Step 1: Run the standalone issue #48 regression executable.**

```bash
build/bin/test_issue48_json_loader.exe
```

Expected: all issue #48 assertions pass with zero failures.

- [ ] **Step 2: Run the existing focused filters.**

```bash
build/bin/tests.exe "[project_file]"
build/bin/tests.exe "[library]"
```

Expected: existing project-file and component-library assertions remain green.

- [ ] **Step 3: Run the existing containment/security regression executable.**

```bash
build/bin/test_path_containment.exe
```

Expected: all existing S1/S2 containment assertions remain green.

- [ ] **Step 4: Run formatting on changed C++ files and check the diff.**

```bash
scripts/format.sh --check
 git diff master...HEAD --check
 git status --short --branch
```

Expected: formatting check succeeds, diff check reports no whitespace errors, and only the design/plan documents plus intended source, test, CMake, and DOX files are changed.

- [ ] **Step 5: Review commit history and summarize intentional scope.**

```bash
git log --oneline master..HEAD
git diff --stat master...HEAD
```

Confirm commits are atomic: design, test, library fix, project fix, and DOX documentation. Report that extension-manifest parsing and unrelated parsers were intentionally not changed.
