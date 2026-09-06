// Issue #80 regression coverage: extension-execution hardening after the
// trust-gating work (issue #45). Standalone executable (not part of
// test_extensions.cpp) because the MinGW-w64 toolchain silently drops
// TEST_CASE registrations beyond the per-binary ceiling; see tests/CMakeLists.txt.
//
// Covered here:
//  - traversal / special-character extension ids are rejected at manifest
//    parse time, before any filesystem use,
//  - the runner executes each invocation in a fresh, isolated workspace,
//  - oversized result files are rejected before JSON parsing,
//  - app-level canonical containment stops a hand-built (parse-bypassing)
//    manifest from pushing the workspace outside the temporary run root.

#include "app.h"
#include "extension_manifest.h"
#include "external_tool_runner.h"

#include <nlohmann/json.hpp>

#include <catch2/catch_test_macros.hpp>

#include "imgui.h"
#include "imnodes.h"
#include "implot.h"
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

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

namespace fs = std::filesystem;

namespace {

struct ScopedRemove {
    fs::path path;

    ~ScopedRemove() {
        std::error_code ec;
        fs::remove_all(path, ec);
    }
};

void writeManifestJson(const fs::path &dir, const nlohmann::json &manifest) {
    fs::create_directories(dir);
    std::ofstream out(dir / "plugin.json");
    out << manifest.dump();
}

ExtensionManifest makeToolManifest(const fs::path &root, const std::string &id,
                                   const fs::path &entry_path) {
    ExtensionManifest manifest;
    manifest.kind = ExtensionKind::ExternalTool;
    manifest.id = id;
    manifest.name = id;
    manifest.version = "1.0.0";
    manifest.root_dir = root;
    manifest.entry_path = entry_path;
    return manifest;
}

} // namespace

TEST_CASE("extension manifest rejects ids unsafe for filesystem use", "[extensions][manifest]") {
    // Regression for issue #80: manifest ids are turned into workspace path
    // segments at run time, so traversal and special-character ids must be
    // rejected at parse time, before any filesystem use.
    const std::vector<std::string> bad_ids = {
        "../../escape80", "..\\escape80", "vendor/tool", "vendor tool", ".", ".."};
    for (std::size_t i = 0; i < bad_ids.size(); ++i) {
        const fs::path root =
            fs::temp_directory_path() / ("rfsim_ext80_bad_id_" + std::to_string(i));
        writeManifestJson(root, {{"schema_version", 1},
                                 {"id", bad_ids[i]},
                                 {"name", "Bad Tool"},
                                 {"version", "1.0.0"},
                                 {"kind", "external-tool"},
                                 {"capabilities", {"generator"}},
                                 {"entry", "bin/tool.py"}});

        std::vector<ExtensionValidationIssue> issues;
        const auto manifest = parseExtensionManifest(root / "plugin.json", issues);

        INFO("id: " << bad_ids[i]);
        REQUIRE_FALSE(manifest.has_value());
        REQUIRE_FALSE(issues.empty());
        REQUIRE(issues.front().field == "id");
    }
}

TEST_CASE("external tool runner rejects oversized result files before parsing",
          "[extensions][runner]") {
    // Regression for issue #80: the result file must be size-bounded before
    // any JSON parsing happens, so a runaway tool cannot force unbounded
    // parsing of a huge output.
    ExternalToolRunner runner;
    const fs::path work_root = fs::temp_directory_path() / "rfsim_ext80_runner_oversize";
    ScopedRemove cleanup{work_root};

    const fs::path tool_dir = work_root / "tool";
    const fs::path script_path = tool_dir / "big_result_tool.py";
    fs::create_directories(tool_dir);
    {
        std::ofstream out(script_path);
        out << "import pathlib, sys\n"
            << "result = pathlib.Path(sys.argv[sys.argv.index(\"--result\") + 1])\n"
            << "# Well above ExternalToolRunner::maxResultFileBytes (1 MiB): the\n"
            << "# runner must reject on size alone, before any JSON parsing.\n"
            << "result.write_bytes(b\"x\" * (2 * 1024 * 1024))\n";
    }

    const ExtensionManifest manifest = makeToolManifest(tool_dir, "vendor.big-result", script_path);

    const fs::path selected_path = work_root / "inputs" / "selection.s2p";
    fs::create_directories(selected_path.parent_path());
    std::ofstream(selected_path) << "touchstone input";

    const ExternalToolRequest request{"1", "Generate", work_root, selected_path,
                                      work_root / "work"};

    const auto result = runner.run(manifest, request);

    REQUIRE_FALSE(result.ok);
    REQUIRE(result.exit_code == 0);
    REQUIRE(result.message == "result file too large");
    REQUIRE(fs::exists(result.result_path));
}

TEST_CASE("external tool runner gives each invocation an isolated workspace",
          "[extensions][runner]") {
    // Regression for issue #80: concurrent or sequential invocations must not
    // reuse a workspace, or stale request/result files leak between runs.
    ExternalToolRunner runner;
    const fs::path fixture_root =
        fs::path(PROJECT_SOURCE_DIR) / "tests" / "fixtures" / "extensions";
    const fs::path work_root = fs::temp_directory_path() / "rfsim_ext80_runner_isolated";
    ScopedRemove cleanup{work_root};

    const ExtensionManifest manifest =
        makeToolManifest(fixture_root, "vendor.echo-generator", fixture_root / "echo_tool.py");

    const fs::path selected_path = work_root / "inputs" / "selection.s2p";
    fs::create_directories(selected_path.parent_path());
    std::ofstream(selected_path) << "touchstone input";

    const ExternalToolRequest request{"1", "Generate", work_root, selected_path,
                                      work_root / "work"};

    const auto first = runner.run(manifest, request);
    REQUIRE(first.ok);

    // Plant a stale result where the first invocation wrote; the next run gets
    // a brand-new workspace and must not see it.
    std::ofstream(first.result_path) << "{\"result_type\":\"stale\"}\n";

    const auto second = runner.run(manifest, request);

    REQUIRE(second.ok);
    REQUIRE(second.message == "tool ok");
    REQUIRE(first.work_dir != second.work_dir);
    REQUIRE(first.work_dir.parent_path() == request.work_dir);
    REQUIRE(second.work_dir.parent_path() == request.work_dir);
    REQUIRE(fs::exists(second.work_dir / "request.json"));
    REQUIRE(fs::exists(second.result_path));
    REQUIRE(first.result_path != second.result_path);
}

TEST_CASE_METHOD(ImGuiFixture, "app runExternalTool refuses a workspace escaping the run root",
                 "[extensions][app]") {
    // Regression for issue #80: even a hand-built manifest (which bypasses
    // parse-time id validation) must not be able to push the extension
    // workspace outside the canonical temporary run root.
    const fs::path workspace_root = fs::temp_directory_path() / "rf-sim-extension-run";
    std::error_code canonical_ec;
    const fs::path escape_target =
        fs::weakly_canonical(workspace_root / ".." / ".." / "escape80_issue80", canonical_ec);
    ScopedRemove cleanup{escape_target};

    ExtensionManifest manifest = makeToolManifest(
        fs::temp_directory_path(), "../../escape80_issue80", escape_target / "tool.py");

    RfSimulatorApp app;
    app.runExternalTool(manifest);

    REQUIRE(app.testExtensionResultMessage().find("Extension run failed") != std::string::npos);
    REQUIRE(app.testExtensionResultMessage().find("not allowed") != std::string::npos);
    REQUIRE_FALSE(fs::exists(escape_target));
}
