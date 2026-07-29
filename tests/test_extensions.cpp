#include "extension_manifest.h"
#include "extension_manager.h"

#include <algorithm>

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace {

fs::path writeManifest(const fs::path &dir, const std::string &body) {
    fs::create_directories(dir);
    const fs::path path = dir / "plugin.json";
    std::ofstream out(path);
    out << body;
    return path;
}

} // namespace

TEST_CASE("extension manifest parses valid external-tool generator", "[extensions][manifest]") {
    const fs::path root = fs::temp_directory_path() / "rfsim_ext_manifest_ok";
    const fs::path manifest_path = writeManifest(
        root,
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
    REQUIRE(manifest->capabilities == std::vector<ExtensionCapability>{ExtensionCapability::Generator});
    REQUIRE(manifest->entry_path == fs::weakly_canonical(root / "bin/adapter.py"));
    REQUIRE(manifest->menus.size() == 1);
    REQUIRE(manifest->menus.front().location == "tools");
    REQUIRE(manifest->menus.front().label == "Generate amplifier data...");
}

TEST_CASE("extension manifest rejects entry escaping plugin root", "[extensions][manifest]") {
    const fs::path root = fs::temp_directory_path() / "rfsim_ext_manifest_escape";
    const fs::path manifest_path = writeManifest(
        root,
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
    const fs::path manifest_path = writeManifest(
        root,
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

TEST_CASE("extension manager discovers project-local data packs", "[extensions][discovery]") {
    ExtensionManager mgr;
    const fs::path project_root = fs::temp_directory_path() / "rfsim_ext_project";
    const fs::path manifest_path = writeManifest(
        project_root / "rf-sim-extensions" / "project-pack",
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
    const fs::path manifest_path = writeManifest(
        project_root / "rf-sim-extensions" / "bad",
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
