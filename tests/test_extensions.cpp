#include "app.h"
#include "extension_manager.h"
#include "extension_manifest.h"

#include "external_tool_runner.h"

#include <nlohmann/json.hpp>

#include <algorithm>

#include <catch2/catch_test_macros.hpp>

#include "imgui.h"
#include "imnodes.h"
#include "implot.h"
#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>
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

fs::path writeManifest(const fs::path &dir, const std::string &body) {
    fs::create_directories(dir);
    const fs::path path = dir / "plugin.json";
    std::ofstream out(path);
    out << body;
    return path;
}

struct ScopedRemove {
    fs::path path;

    ~ScopedRemove() {
        std::error_code ec;
        fs::remove_all(path, ec);
    }
};

} // namespace

TEST_CASE("extension manifest parses valid external-tool generator", "[extensions][manifest]") {
    const fs::path root = fs::temp_directory_path() / "rfsim_ext_manifest_ok";
    const fs::path manifest_path = writeManifest(root,
                                                 R"json({
            "schema_version": 1,
            "id": "vendor.amp-f-generator",
            "name": "AMP_F Generator",
            "version": "1.0.0",
            "kind": "external-tool",
            "capabilities": ["generator"],
            "entry": "bin/adapter.py",
            "menus": [{"location": "tools", "label": "Generate amplifier data..."}],
            "compat": {"min_app_version": "0.14.0"}
        })json");

    std::vector<ExtensionValidationIssue> issues;
    const auto manifest = parseExtensionManifest(manifest_path, issues);

    REQUIRE(manifest.has_value());
    REQUIRE(issues.empty());
    REQUIRE(manifest->kind == ExtensionKind::ExternalTool);
    REQUIRE(manifest->capabilities ==
            std::vector<ExtensionCapability>{ExtensionCapability::Generator});
    REQUIRE(manifest->entry_path == fs::weakly_canonical(root / "bin/adapter.py"));
    REQUIRE(manifest->menus.size() == 1);
    REQUIRE(manifest->menus.front().location == "tools");
    REQUIRE(manifest->menus.front().label == "Generate amplifier data...");
}

TEST_CASE("extension manifest rejects entry escaping plugin root", "[extensions][manifest]") {
    const fs::path root = fs::temp_directory_path() / "rfsim_ext_manifest_escape";
    const fs::path manifest_path = writeManifest(root,
                                                 R"json({
            "schema_version": 1,
            "id": "vendor.bad",
            "name": "Bad Tool",
            "version": "1.0.0",
            "kind": "external-tool",
            "capabilities": ["generator"],
            "entry": "../python.exe"
        })json");

    std::vector<ExtensionValidationIssue> issues;
    const auto manifest = parseExtensionManifest(manifest_path, issues);

    REQUIRE_FALSE(manifest.has_value());
    REQUIRE_FALSE(issues.empty());
    REQUIRE(issues.front().field == "entry");
}

TEST_CASE("extension manifest rejects unsupported capability", "[extensions][manifest]") {
    const fs::path root = fs::temp_directory_path() / "rfsim_ext_manifest_cap";
    const fs::path manifest_path = writeManifest(root,
                                                 R"json({
            "schema_version": 1,
            "id": "vendor.viewer",
            "name": "Viewer",
            "version": "1.0.0",
            "kind": "external-tool",
            "capabilities": ["viewer"],
            "entry": "bin/viewer.exe"
        })json");

    std::vector<ExtensionValidationIssue> issues;
    const auto manifest = parseExtensionManifest(manifest_path, issues);

    REQUIRE_FALSE(manifest.has_value());
    REQUIRE_FALSE(issues.empty());
    REQUIRE(issues.front().field == "capabilities[0]");
}

TEST_CASE("extension manifest preserves non-tools menu locations", "[extensions][manifest]") {
    const fs::path root = fs::temp_directory_path() / "rfsim_ext_manifest_menu";
    const fs::path manifest_path = writeManifest(root,
                                                 R"json({
            "schema_version": 1,
            "id": "vendor.bad-menu",
            "name": "Bad Menu",
            "version": "1.0.0",
            "kind": "external-tool",
            "capabilities": ["generator"],
            "entry": "bin/tool.py",
            "menus": [{"location": "toolbar", "label": "Run"}]
        })json");

    std::vector<ExtensionValidationIssue> issues;
    const auto manifest = parseExtensionManifest(manifest_path, issues);

    REQUIRE(manifest.has_value());
    REQUIRE(issues.empty());
    REQUIRE(manifest->menus.size() == 1);
    REQUIRE(manifest->menus.front().location == "toolbar");
    REQUIRE(manifest->menus.front().label == "Run");
}

TEST_CASE("extension manager discovers project-local data packs", "[extensions][discovery]") {
    ExtensionManager mgr;
    const fs::path project_root = fs::temp_directory_path() / "rfsim_ext_project";
    const fs::path manifest_path =
        writeManifest(project_root / "rf-sim-extensions" / "project-pack",
                      R"json({
            "schema_version": 1,
            "id": "project.pack",
            "name": "Project Pack",
            "version": "1.0.0",
            "kind": "data-pack"
        })json");

    mgr.rescan(project_root);

    const auto &records = mgr.all();
    const auto record_it =
        std::find_if(records.begin(), records.end(), [&](const ExtensionRecord &record) {
            return record.manifest_path == manifest_path;
        });

    REQUIRE(record_it != records.end());
    REQUIRE(record_it->status == ExtensionStatusKind::Ok);
    REQUIRE(record_it->manifest.has_value());
    REQUIRE(record_it->manifest->kind == ExtensionKind::DataPack);

    const auto packs = mgr.dataPacks();
    REQUIRE(std::find(packs.begin(), packs.end(), &*record_it->manifest) != packs.end());
}

TEST_CASE("extension manager keeps invalid manifests visible", "[extensions][discovery]") {
    ExtensionManager mgr;
    const fs::path project_root = fs::temp_directory_path() / "rfsim_ext_invalid";
    const fs::path manifest_path = writeManifest(project_root / "rf-sim-extensions" / "bad",
                                                 R"json({"kind": "external-tool"})json");

    mgr.rescan(project_root);

    const auto &records = mgr.all();
    const auto record_it =
        std::find_if(records.begin(), records.end(), [&](const ExtensionRecord &record) {
            return record.manifest_path == manifest_path;
        });

    REQUIRE(record_it != records.end());
    REQUIRE(record_it->status == ExtensionStatusKind::Invalid);
    REQUIRE_FALSE(record_it->manifest.has_value());
    REQUIRE_FALSE(record_it->issues.empty());
}

TEST_CASE("extension manager prefers project-local copies over built-in copies",
          "[extensions][discovery]") {
    const fs::path builtin_root = fs::path(PROJECT_SOURCE_DIR) / "extensions" / "rfsim_shadow_case";
    const fs::path project_root = fs::temp_directory_path() / "rfsim_ext_shadow";
    const fs::path builtin_manifest = writeManifest(builtin_root,
                                                    R"json({
            "schema_version": 1,
            "id": "shared.pack",
            "name": "Built-in Pack",
            "version": "1.0.0",
            "kind": "data-pack"
        })json");
    const fs::path project_manifest =
        writeManifest(project_root / "rf-sim-extensions" / "shared-pack",
                      R"json({
            "schema_version": 1,
            "id": "shared.pack",
            "name": "Project Pack",
            "version": "2.0.0",
            "kind": "data-pack"
        })json");
    ScopedRemove cleanup{builtin_root};

    REQUIRE(fs::exists(builtin_manifest));

    ExtensionManager mgr;
    mgr.rescan(project_root);

    const auto &records = mgr.all();
    const auto record_it =
        std::find_if(records.begin(), records.end(), [&](const ExtensionRecord &record) {
            return record.manifest && record.manifest->id == "shared.pack";
        });

    REQUIRE(record_it != records.end());
    REQUIRE(record_it->manifest_path == project_manifest);
    REQUIRE(record_it->manifest->name == "Project Pack");
    REQUIRE(record_it->status == ExtensionStatusKind::Ok);
    REQUIRE(std::count_if(records.begin(), records.end(), [&](const ExtensionRecord &record) {
                return record.manifest && record.manifest->id == "shared.pack";
            }) == 1);

    const auto packs = mgr.dataPacks();
    REQUIRE(std::find_if(packs.begin(), packs.end(), [&](const ExtensionManifest *manifest) {
                return manifest->id == "shared.pack";
            }) != packs.end());
}

TEST_CASE("extension manager excludes malformed compatibility manifests from active queries",
          "[extensions][discovery]") {
    ExtensionManager mgr;
    const fs::path project_root = fs::temp_directory_path() / "rfsim_ext_incompatible";
    const fs::path manifest_path = writeManifest(project_root / "rf-sim-extensions" / "future-pack",
                                                 R"json({
            "schema_version": 1,
            "id": "future.pack",
            "name": "Future Pack",
            "version": "1.0.0",
            "kind": "data-pack",
            "compat": {"min_app_version": "99-beta"}
        })json");

    mgr.rescan(project_root);

    const auto &records = mgr.all();
    const auto record_it =
        std::find_if(records.begin(), records.end(), [&](const ExtensionRecord &record) {
            return record.manifest_path == manifest_path;
        });

    REQUIRE(record_it != records.end());
    REQUIRE(record_it->status == ExtensionStatusKind::Incompatible);
    REQUIRE(record_it->manifest.has_value());
    REQUIRE(std::count_if(records.begin(), records.end(), [&](const ExtensionRecord &record) {
                return record.manifest && record.manifest->id == "future.pack";
            }) == 1);

    const auto packs = mgr.dataPacks();
    REQUIRE(std::find_if(packs.begin(), packs.end(), [&](const ExtensionManifest *manifest) {
                return manifest->id == "future.pack";
            }) == packs.end());

    const auto tools = mgr.externalTools();
    REQUIRE(std::find_if(tools.begin(), tools.end(), [&](const ExtensionManifest *manifest) {
                return manifest->id == "future.pack";
            }) == tools.end());
}

TEST_CASE("external tool runner writes request file and reads result file",
          "[extensions][runner]") {
    ExternalToolRunner runner;
    const fs::path fixture_root =
        fs::path(PROJECT_SOURCE_DIR) / "tests" / "fixtures" / "extensions";
    const fs::path work_root = fs::temp_directory_path() / "rfsim_ext_runner_ok";
    ScopedRemove cleanup{work_root};

    ExtensionManifest manifest;
    manifest.kind = ExtensionKind::ExternalTool;
    manifest.id = "vendor.echo-generator";
    manifest.name = "Echo Generator";
    manifest.version = "1.0.0";
    manifest.root_dir = fixture_root;
    manifest.entry_path = fixture_root / "echo_tool.py";

    const fs::path selected_path = work_root / "inputs" / "selection.s2p";
    fs::create_directories(selected_path.parent_path());
    std::ofstream(selected_path) << "touchstone input";

    const ExternalToolRequest request{
        "1",           "Generate",         work_root,
        selected_path, work_root / "work", work_root / "work" / "result.json"};

    const auto result = runner.run(manifest, request);

    REQUIRE(result.ok);
    REQUIRE(result.exit_code == 0);
    REQUIRE(result.message == "tool ok");
    REQUIRE(result.work_dir == request.work_dir);
    REQUIRE(fs::exists(request.work_dir / "request.json"));
    REQUIRE(fs::exists(request.result_path));

    const nlohmann::json request_json =
        nlohmann::json::parse(std::ifstream(request.work_dir / "request.json"), nullptr, false);
    REQUIRE_FALSE(request_json.is_discarded());
    REQUIRE(request_json["action_label"] == "Generate");
    REQUIRE(request_json["project_root"] == work_root.generic_string());
    REQUIRE(request_json["selected_path"] == selected_path.generic_string());
    REQUIRE(request_json["result_path"] == request.result_path.generic_string());

    const nlohmann::json result_json =
        nlohmann::json::parse(std::ifstream(request.result_path), nullptr, false);
    REQUIRE_FALSE(result_json.is_discarded());
    REQUIRE(result_json["result_type"] == "report_created");
    REQUIRE(result_json["request"]["action_label"] == "Generate");
}

TEST_CASE("external tool runner reports missing result as failure", "[extensions][runner]") {
    ExternalToolRunner runner;
    const fs::path fixture_root =
        fs::path(PROJECT_SOURCE_DIR) / "tests" / "fixtures" / "extensions";
    const fs::path work_root = fs::temp_directory_path() / "rfsim_ext_runner_fail";
    ScopedRemove cleanup{work_root};

    ExtensionManifest manifest;
    manifest.kind = ExtensionKind::ExternalTool;
    manifest.id = "vendor.noop";
    manifest.name = "Noop Tool";
    manifest.version = "1.0.0";
    manifest.root_dir = fixture_root;
    manifest.entry_path = fixture_root / "noop_tool.py";

    const ExternalToolRequest request{"1",
                                      "Check",
                                      work_root,
                                      work_root / "input.txt",
                                      work_root / "work",
                                      work_root / "work" / "result.json"};

    const auto result = runner.run(manifest, request);

    REQUIRE_FALSE(result.ok);
    REQUIRE(result.exit_code == 0);
    REQUIRE(result.message == "result file missing");
    REQUIRE(fs::exists(request.work_dir / "request.json"));
    REQUIRE_FALSE(fs::exists(request.result_path));
}

TEST_CASE_METHOD(ImGuiFixture, "app refreshExtensions discovers project-local data packs",
                 "[extensions][app]") {
    const fs::path project_root = fs::temp_directory_path() / "rfsim_ext_app";
    ScopedRemove cleanup{project_root};

    const fs::path manifest_path =
        writeManifest(project_root / "rf-sim-extensions" / "project-pack",
                      R"json({
            "schema_version": 1,
            "id": "project.pack",
            "name": "Project Pack",
            "version": "1.0.0",
            "kind": "data-pack"
        })json");

    RfSimulatorApp app;
    app.m_current_project_path = (project_root / "demo.rfsim").string();
    app.refreshExtensions();

    const auto &records = app.testExtensionManager().all();
    REQUIRE(std::find_if(records.begin(), records.end(), [&](const ExtensionRecord &record) {
                return record.manifest_path == manifest_path;
            }) != records.end());

    const auto packs = app.testExtensionManager().dataPacks();
    REQUIRE(std::find_if(packs.begin(), packs.end(), [&](const ExtensionManifest *manifest) {
                return manifest->id == "project.pack";
            }) != packs.end());
}

TEST_CASE_METHOD(ImGuiFixture, "app runExternalTool records success message", "[extensions][app]") {
    const fs::path project_root = fs::temp_directory_path() / "rfsim_ext_app_tool";
    ScopedRemove cleanup{project_root};

    const fs::path tool_dir = project_root / "rf-sim-extensions" / "echo-tool";
    const fs::path script_path = tool_dir / "bin" / "echo_tool.py";
    fs::create_directories(script_path.parent_path());
    {
        std::ofstream out(script_path);
        out << "#!/usr/bin/env python3\n"
            << "import json\n"
            << "import pathlib\n"
            << "import sys\n\n"
            << "request = pathlib.Path(sys.argv[sys.argv.index(\"--request\") + 1])\n"
            << "result = pathlib.Path(sys.argv[sys.argv.index(\"--result\") + 1])\n"
            << "request_json = json.loads(request.read_text(encoding=\"utf-8\"))\n"
            << "result.write_text(json.dumps({\"result_type\": \"report_created\", "
               "\"message\": \"tool ok\", \"request\": request_json}, indent=2) + \"\\n\", "
               "encoding=\"utf-8\")\n";
    }

    writeManifest(tool_dir,
                  R"json({
            "schema_version": 1,
            "id": "project.echo",
            "name": "Project Echo",
            "version": "1.0.0",
            "kind": "external-tool",
            "capabilities": ["generator"],
            "entry": "bin/echo_tool.py",
            "menus": [{"location": "tools", "label": "Echo"}]
        })json");

    RfSimulatorApp app;
    app.m_current_project_path = (project_root / "demo.rfsim").string();
    app.refreshExtensions();

    const auto &tools = app.testExtensionManager().externalTools();
    REQUIRE_FALSE(tools.empty());

    app.runExternalTool(*tools.front());

    REQUIRE(app.testExtensionResultMessage().find("Extension run succeeded") != std::string::npos);
}

TEST_CASE_METHOD(ImGuiFixture, "app runExternalTool passes selected menu label to the tool",
                 "[extensions][app]") {
    const fs::path project_root = fs::temp_directory_path() / "rfsim_ext_app_tool_action";
    ScopedRemove cleanup{project_root};

    const fs::path tool_dir = project_root / "rf-sim-extensions" / "multi-action-tool";
    const fs::path script_path = tool_dir / "bin" / "action_tool.py";
    const fs::path action_path = tool_dir / "last_action.txt";
    fs::create_directories(script_path.parent_path());
    {
        std::ofstream out(script_path);
        out << "#!/usr/bin/env python3\n"
            << "import json\n"
            << "import pathlib\n"
            << "import sys\n\n"
            << "request = pathlib.Path(sys.argv[sys.argv.index(\"--request\") + 1])\n"
            << "result = pathlib.Path(sys.argv[sys.argv.index(\"--result\") + 1])\n"
            << "request_json = json.loads(request.read_text(encoding=\"utf-8\"))\n"
            << "pathlib.Path(r\"" << action_path.generic_string()
            << "\").write_text("
               "request_json[\"action_label\"], encoding=\"utf-8\")\n"
            << "result.write_text(json.dumps({\"result_type\": \"report_created\", "
               "\"message\": \"tool ok\"}) + \"\\n\", encoding=\"utf-8\")\n";
    }

    writeManifest(tool_dir,
                  R"json({
            "schema_version": 1,
            "id": "project.multi-action",
            "name": "Project Multi Action",
            "version": "1.0.0",
            "kind": "external-tool",
            "capabilities": ["generator"],
            "entry": "bin/action_tool.py",
            "menus": [
                {"location": "tools", "label": "Generate"},
                {"location": "tools", "label": "Import"}
            ]
        })json");

    RfSimulatorApp app;
    app.m_current_project_path = (project_root / "demo.rfsim").string();
    app.refreshExtensions();

    const auto &tools = app.testExtensionManager().externalTools();
    REQUIRE(std::size(tools) == 1);

    app.runExternalTool(*tools.front(), "Import");

    REQUIRE(fs::exists(action_path));
    REQUIRE(std::ifstream(action_path).good());
    std::string action_label;
    std::ifstream action_in(action_path);
    std::getline(action_in, action_label);
    REQUIRE(action_label == "Import");
}

TEST_CASE_METHOD(ImGuiFixture, "app externalToolActions preserves declared menu actions",
                 "[extensions][app]") {
    const fs::path project_root = fs::temp_directory_path() / "rfsim_ext_app_tool_actions";
    ScopedRemove cleanup{project_root};

    const fs::path tool_dir = project_root / "rf-sim-extensions" / "multi-action-tool";
    const fs::path script_path = tool_dir / "bin" / "action_tool.py";
    fs::create_directories(script_path.parent_path());
    {
        std::ofstream out(script_path);
        out << "#!/usr/bin/env python3\n"
            << "import json\n"
            << "import pathlib\n"
            << "import sys\n\n"
            << "result = pathlib.Path(sys.argv[sys.argv.index(\"--result\") + 1])\n"
            << "result.write_text(json.dumps({\"result_type\": \"report_created\", "
               "\"message\": \"tool ok\"}) + \"\\n\", encoding=\"utf-8\")\n";
    }

    writeManifest(tool_dir,
                  R"json({
            "schema_version": 1,
            "id": "project.multi-action",
            "name": "Project Multi Action",
            "version": "1.0.0",
            "kind": "external-tool",
            "capabilities": ["generator"],
            "entry": "bin/action_tool.py",
            "menus": [
                {"location": "tools", "label": "Generate"},
                {"location": "toolbar", "label": "Import"}
            ]
        })json");

    RfSimulatorApp app;
    app.m_current_project_path = (project_root / "demo.rfsim").string();
    app.refreshExtensions();

    const auto &tools = app.testExtensionManager().externalTools();
    REQUIRE(std::size(tools) == 1);

    const auto actions = app.externalToolActions(*tools.front());
    REQUIRE(actions.size() == 2);
    REQUIRE(actions[0].location == "tools");
    REQUIRE(actions[0].label == "Generate");
    REQUIRE(actions[1].location == "toolbar");
    REQUIRE(actions[1].label == "Import");
}

TEST_CASE_METHOD(ImGuiFixture, "app runExternalTool does not reuse stale result files",
                 "[extensions][app]") {
    const fs::path project_root = fs::temp_directory_path() / "rfsim_ext_app_tool_stale";
    ScopedRemove cleanup{project_root};

    const fs::path tool_dir = project_root / "rf-sim-extensions" / "flaky-tool";
    const fs::path script_path = tool_dir / "bin" / "flaky_tool.py";
    fs::create_directories(script_path.parent_path());
    {
        std::ofstream out(script_path);
        out << "#!/usr/bin/env python3\n"
            << "import json\n"
            << "import pathlib\n"
            << "import sys\n\n"
            << "result = pathlib.Path(sys.argv[sys.argv.index(\"--result\") + 1])\n"
            << "result.write_text(json.dumps({\"result_type\": \"report_created\", "
               "\"message\": \"first run ok\"}) + \"\\n\", encoding=\"utf-8\")\n";
    }

    writeManifest(tool_dir,
                  R"json({
            "schema_version": 1,
            "id": "project.flaky-tool",
            "name": "Project Flaky Tool",
            "version": "1.0.0",
            "kind": "external-tool",
            "capabilities": ["generator"],
            "entry": "bin/flaky_tool.py",
            "menus": [{"location": "tools", "label": "Run"}]
        })json");

    RfSimulatorApp app;
    app.m_current_project_path = (project_root / "demo.rfsim").string();
    app.refreshExtensions();

    const auto &tools = app.testExtensionManager().externalTools();
    REQUIRE(std::size(tools) == 1);

    app.runExternalTool(*tools.front(), "Run");
    REQUIRE(app.testExtensionResultMessage().find("Extension run succeeded") != std::string::npos);

    {
        std::ofstream out(script_path);
        out << "#!/usr/bin/env python3\n"
            << "import sys\n"
            << "sys.exit(0)\n";
    }

    app.runExternalTool(*tools.front(), "Run");

    REQUIRE(app.testExtensionResultMessage().find("Extension run failed") != std::string::npos);
    REQUIRE(app.testExtensionResultMessage().find("result file missing") != std::string::npos);
}
