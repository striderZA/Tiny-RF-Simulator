// Issue #45 regression coverage: project-local extensions cannot execute
// until the user explicitly trusts them, and a duplicate extension id is
// reported instead of silently replacing the manifest that claimed it first.
// Standalone executable (not part of test_extensions.cpp) because the
// MinGW-w64 toolchain silently drops TEST_CASE registrations beyond the
// per-binary ceiling; see tests/CMakeLists.txt.
//
// Covered here:
//  - ExtensionTrustStore persistence: round trip, fail-closed loading,
//    approvals scoped to one directory, revocation,
//  - the app gate: an untrusted project-local tool refuses to run before any
//    workspace is created and raises the trust prompt,
//  - grant / deny / revoke transitions, including that trusting is not running,
//  - built-in, global, and never-rescanned roots are not gated,
//  - an external-tool id collision leaves the built-in launchable and the
//    project-local copy Shadowed.

#include "app.h"
#include "extension_manager.h"
#include "extension_manifest.h"
#include "extension_trust_store.h"

#include <nlohmann/json.hpp>

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

struct ScopedRemove {
    fs::path path;

    ~ScopedRemove() {
        std::error_code ec;
        fs::remove_all(path, ec);
    }
};

fs::path writeManifest(const fs::path &dir, const nlohmann::json &body) {
    fs::create_directories(dir);
    const fs::path path = dir / "plugin.json";
    std::ofstream out(path);
    out << body.dump();
    return path;
}

std::string genericRoot(const fs::path &root) {
    return fs::weakly_canonical(root).generic_string();
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

void writeStoreFile(const fs::path &path, const std::string &body) {
    fs::create_directories(path.parent_path());
    std::ofstream out(path);
    out << body;
}

// A project-local tool that records its own execution, so a test can prove the
// entry point never ran.
struct ToolFixture {
    fs::path project_root;
    fs::path tool_dir;
    fs::path side_effect;
    std::string id;
};

ToolFixture makeProjectTool(const std::string &name, const std::string &id) {
    ToolFixture fixture;
    fixture.project_root = fs::temp_directory_path() / name;
    fixture.tool_dir = fixture.project_root / "rf-sim-extensions" / "echo-tool";
    fixture.side_effect = fixture.tool_dir / "ran.txt";
    fixture.id = id;

    const fs::path script_path = fixture.tool_dir / "bin" / "echo_tool.py";
    fs::create_directories(script_path.parent_path());
    {
        std::ofstream out(script_path);
        out << "#!/usr/bin/env python3\n"
            << "import json\n"
            << "import pathlib\n"
            << "import sys\n\n"
            << "pathlib.Path(r\"" << fixture.side_effect.generic_string()
            << "\").write_text(\"ran\", encoding=\"utf-8\")\n"
            << "result = pathlib.Path(sys.argv[sys.argv.index(\"--result\") + 1])\n"
            << "result.write_text(json.dumps({\"result_type\": \"report_created\", "
               "\"message\": \"tool ok\"}) + \"\\n\", encoding=\"utf-8\")\n";
    }

    writeManifest(fixture.tool_dir, {{"schema_version", 1},
                                     {"id", id},
                                     {"name", "Issue45 Echo"},
                                     {"version", "1.0.0"},
                                     {"kind", "external-tool"},
                                     {"capabilities", {"generator"}},
                                     {"entry", "bin/echo_tool.py"},
                                     {"menus", {{{"location", "tools"}, {"label", "Echo"}}}}});

    return fixture;
}

const ExtensionManifest *toolById(RfSimulatorApp &app, const std::string &id) {
    for (const auto *tool : app.testExtensionManager().externalTools()) {
        if (tool && tool->id == id)
            return tool;
    }
    return nullptr;
}

} // namespace

TEST_CASE("trust store round-trips an approval through its file", "[issue45][trust-store]") {
    const fs::path root_a = fs::temp_directory_path() / "rfsim_ext45_store_a";
    const fs::path store_path = fs::temp_directory_path() / "rfsim_ext45_store.json";
    ScopedRemove cleanup_a{root_a};
    ScopedRemove cleanup_store{store_path};
    fs::create_directories(root_a);

    ExtensionTrustStore store(store_path);
    REQUIRE_FALSE(store.isApproved(root_a));

    const ExtensionManifest manifest =
        makeToolManifest(root_a, "project.echo45", root_a / "bin" / "echo_tool.py");
    REQUIRE(store.approve(manifest));
    REQUIRE(store.isApproved(root_a));
    REQUIRE(fs::exists(store_path));

    const nlohmann::json persisted =
        nlohmann::json::parse(std::ifstream(store_path), nullptr, false);
    REQUIRE_FALSE(persisted.is_discarded());
    REQUIRE(persisted["schema_version"] == 1);
    REQUIRE(persisted["approvals"].size() == 1);
    REQUIRE(persisted["approvals"][0]["root"] == genericRoot(root_a));
    REQUIRE(persisted["approvals"][0]["id"] == "project.echo45");
    REQUIRE(persisted["approvals"][0]["version"] == "1.0.0");
    REQUIRE(persisted["approvals"][0]["entry_path"] ==
            (root_a / "bin" / "echo_tool.py").generic_string());

    ExtensionTrustStore reloaded(store_path);
    REQUIRE(reloaded.isApproved(root_a));
    const auto entry = reloaded.entryFor(root_a);
    REQUIRE(entry.has_value());
    REQUIRE(entry->id == "project.echo45");
    REQUIRE(entry->entry_path == (root_a / "bin" / "echo_tool.py").generic_string());

    REQUIRE(reloaded.revoke(root_a));
    REQUIRE_FALSE(reloaded.isApproved(root_a));
    REQUIRE_FALSE(reloaded.entryFor(root_a).has_value());

    const nlohmann::json after_revoke =
        nlohmann::json::parse(std::ifstream(store_path), nullptr, false);
    REQUIRE_FALSE(after_revoke.is_discarded());
    REQUIRE(after_revoke["approvals"].empty());
}

TEST_CASE("trust store fails closed on a degraded file", "[issue45][trust-store]") {
    const fs::path root = fs::temp_directory_path() / "rfsim_ext45_failclosed";
    const fs::path store_path = fs::temp_directory_path() / "rfsim_ext45_failclosed.json";
    ScopedRemove cleanup_root{root};
    ScopedRemove cleanup_store{store_path};
    fs::create_directories(root);

    const char *degraded[] = {
        "this is not json",
        R"json({"schema_version": 99, "approvals": []})json",
        R"json({"approvals": []})json",
        R"json({"schema_version": 1, "approvals": {}})json",
        R"json([])json",
    };
    for (const char *body : degraded) {
        INFO(body);
        writeStoreFile(store_path, body);
        const ExtensionTrustStore store(store_path);
        REQUIRE_FALSE(store.isApproved(root));
    }
}

TEST_CASE("trust store keeps valid approvals alongside malformed entries",
          "[issue45][trust-store]") {
    const fs::path root = fs::temp_directory_path() / "rfsim_ext45_partial";
    const fs::path valid_root = fs::temp_directory_path() / "rfsim_ext45_partial_valid";
    const fs::path store_path = fs::temp_directory_path() / "rfsim_ext45_partial.json";
    ScopedRemove cleanup{root};
    ScopedRemove cleanup_valid{valid_root};
    ScopedRemove cleanup_store{store_path};
    fs::create_directories(root);
    fs::create_directories(valid_root);

    const nlohmann::json degraded_doc = {{"schema_version", 1},
                                         {"approvals",
                                          {3,
                                           {{"root", 123}},
                                           {{"root", genericRoot(root)}},
                                           {{"root", genericRoot(valid_root)},
                                            {"id", "project.valid"},
                                            {"version", "1.0.0"},
                                            {"entry_path", "bin/t.py"}}}}};
    writeStoreFile(store_path, degraded_doc.dump());

    const ExtensionTrustStore store(store_path);
    REQUIRE_FALSE(store.isApproved(root));
    REQUIRE(store.isApproved(valid_root));
}

TEST_CASE("trust approvals do not cross extension roots", "[issue45][trust-store]") {
    const fs::path parent = fs::temp_directory_path() / "rfsim_ext45_sibling";
    const fs::path root_a = parent / "a";
    const fs::path root_b = parent / "b";
    const fs::path store_path = parent / "extension_trust.json";
    ScopedRemove cleanup{parent};
    fs::create_directories(root_a);
    fs::create_directories(root_b);

    ExtensionTrustStore store(store_path);
    REQUIRE(store.approve(makeToolManifest(root_a, "project.a", root_a / "tool.py")));
    REQUIRE(store.isApproved(root_a));
    REQUIRE_FALSE(store.isApproved(root_b));

    // An approval can only be recorded for a root that can be named.
    REQUIRE_FALSE(store.approve(makeToolManifest(fs::path{}, "project.none", fs::path{})));
}

TEST_CASE_METHOD(ImGuiFixture, "app refuses to run an untrusted project-local tool",
                 "[issue45][app-gate]") {
    const ToolFixture fixture = makeProjectTool("rfsim_ext45_refuse", "project.echo45refuse");
    ScopedRemove cleanup{fixture.project_root};
    const fs::path store_path = fixture.project_root / "extension_trust.json";

    RfSimulatorApp app;
    app.m_extension_trust.setStorePath(store_path);
    app.m_current_project_path = (fixture.project_root / "demo.rfsim").string();
    app.refreshExtensions();

    const ExtensionManifest *tool = toolById(app, fixture.id);
    REQUIRE(tool != nullptr);
    REQUIRE(app.extensionRequiresTrust(*tool));

    // Issue #80's workspace root must stay untouched by a refused run.
    const fs::path refused_workspace =
        fs::temp_directory_path() / "rf-sim-extension-run" / fixture.id;
    ScopedRemove cleanup_workspace{refused_workspace};

    app.runExternalTool(*tool);

    REQUIRE(app.testExtensionResultMessage().find("not trusted") != std::string::npos);
    REQUIRE(app.m_show_extension_trust_prompt);
    REQUIRE(app.m_pending_trust_manifest.has_value());
    REQUIRE(app.m_pending_trust_manifest->id == fixture.id);
    REQUIRE_FALSE(fs::exists(fixture.side_effect));
    REQUIRE_FALSE(fs::exists(refused_workspace));
    REQUIRE_FALSE(app.m_extension_trust.isApproved(tool->root_dir));
}

TEST_CASE_METHOD(ImGuiFixture, "granting trust persists an approval and enables a separate run",
                 "[issue45][app-gate]") {
    const ToolFixture fixture = makeProjectTool("rfsim_ext45_grant", "project.echo45grant");
    ScopedRemove cleanup{fixture.project_root};
    const fs::path store_path = fixture.project_root / "extension_trust.json";

    RfSimulatorApp app;
    app.m_extension_trust.setStorePath(store_path);
    app.m_current_project_path = (fixture.project_root / "demo.rfsim").string();
    app.refreshExtensions();

    const ExtensionManifest *tool = toolById(app, fixture.id);
    REQUIRE(tool != nullptr);
    const fs::path tool_root = tool->root_dir;

    app.runExternalTool(*tool);
    REQUIRE(app.m_show_extension_trust_prompt);

    // Trusting is one action, running is another: nothing executed yet.
    app.grantPendingExtensionTrust();
    REQUIRE_FALSE(fs::exists(fixture.side_effect));
    REQUIRE_FALSE(app.m_show_extension_trust_prompt);
    REQUIRE_FALSE(app.m_pending_trust_manifest.has_value());
    REQUIRE(app.m_extension_trust.isApproved(tool_root));
    REQUIRE_FALSE(app.extensionRequiresTrust(*tool));

    app.runExternalTool(*tool);
    REQUIRE(app.testExtensionResultMessage().find("Extension run succeeded") != std::string::npos);
    REQUIRE(fs::exists(fixture.side_effect));
}

TEST_CASE_METHOD(ImGuiFixture, "denying the trust prompt leaves the tool refused",
                 "[issue45][app-gate]") {
    const ToolFixture fixture = makeProjectTool("rfsim_ext45_deny", "project.echo45deny");
    ScopedRemove cleanup{fixture.project_root};
    const fs::path store_path = fixture.project_root / "extension_trust.json";

    RfSimulatorApp app;
    app.m_extension_trust.setStorePath(store_path);
    app.m_current_project_path = (fixture.project_root / "demo.rfsim").string();
    app.refreshExtensions();

    const ExtensionManifest *tool = toolById(app, fixture.id);
    REQUIRE(tool != nullptr);

    app.runExternalTool(*tool);
    REQUIRE(app.m_show_extension_trust_prompt);
    app.denyPendingExtensionTrust();
    REQUIRE_FALSE(app.m_show_extension_trust_prompt);
    REQUIRE_FALSE(app.m_pending_trust_manifest.has_value());
    REQUIRE(app.extensionRequiresTrust(*tool));
    REQUIRE_FALSE(fs::exists(store_path));

    app.runExternalTool(*tool);
    REQUIRE(app.testExtensionResultMessage().find("not trusted") != std::string::npos);
    REQUIRE_FALSE(fs::exists(fixture.side_effect));
}

TEST_CASE_METHOD(ImGuiFixture, "revoking trust re-arms the gate", "[issue45][app-gate]") {
    const ToolFixture fixture = makeProjectTool("rfsim_ext45_revoke", "project.echo45revoke");
    ScopedRemove cleanup{fixture.project_root};
    const fs::path store_path = fixture.project_root / "extension_trust.json";

    RfSimulatorApp app;
    app.m_extension_trust.setStorePath(store_path);
    app.m_current_project_path = (fixture.project_root / "demo.rfsim").string();
    app.refreshExtensions();

    const ExtensionManifest *tool = toolById(app, fixture.id);
    REQUIRE(tool != nullptr);
    const fs::path tool_root = tool->root_dir;

    REQUIRE(app.m_extension_trust.approve(*tool));
    app.runExternalTool(*tool);
    REQUIRE(app.testExtensionResultMessage().find("Extension run succeeded") != std::string::npos);

    // A successful run rescans discovery, so re-resolve the manifest before
    // using it again.
    const ExtensionManifest *rerun = toolById(app, fixture.id);
    REQUIRE(rerun != nullptr);

    REQUIRE(app.m_extension_trust.revoke(tool_root));
    REQUIRE(app.extensionRequiresTrust(*rerun));

    app.runExternalTool(*rerun);
    REQUIRE(app.testExtensionResultMessage().find("not trusted") != std::string::npos);
}

TEST_CASE_METHOD(ImGuiFixture, "extensions outside the project root are not gated",
                 "[issue45][app-gate]") {
    const fs::path project_root = fs::temp_directory_path() / "rfsim_ext45_ungated";
    ScopedRemove cleanup{project_root};
    fs::create_directories(project_root / "rf-sim-extensions");

    ExtensionManager mgr;
    // Before any rescan there is no project root to be inside of.
    REQUIRE_FALSE(mgr.isProjectLocal(
        makeToolManifest(fs::temp_directory_path(), "project.any", fs::path("tool.py"))));

    mgr.rescan(project_root);
    REQUIRE(mgr.projectExtensionRoot() == fs::weakly_canonical(project_root / "rf-sim-extensions"));

    REQUIRE_FALSE(mgr.isProjectLocal(
        makeToolManifest(fs::temp_directory_path(), "project.global", fs::path("t.py"))));
    REQUIRE(mgr.isProjectLocal(makeToolManifest(project_root / "rf-sim-extensions" / "tool",
                                                "project.local", fs::path("t.py"))));
    REQUIRE_FALSE(mgr.isProjectLocal(
        makeToolManifest(project_root / "other-place", "project.other", fs::path("t.py"))));
    REQUIRE_FALSE(mgr.isProjectLocal(makeToolManifest(
        project_root / "rf-sim-extensions-subdir" / "tool", "project.sibling", fs::path("t.py"))));

    RfSimulatorApp app;
    const ExtensionManifest builtin =
        makeToolManifest(fs::temp_directory_path(), "project.builtin", fs::path("tool.py"));
    REQUIRE_FALSE(app.extensionRequiresTrust(builtin));
}

TEST_CASE("extension manager flags a duplicate external-tool id as shadowed",
          "[issue45][discovery]") {
    const fs::path builtin_root =
        fs::path(PROJECT_SOURCE_DIR) / "extensions" / "rfsim_shadow_tool_case";
    const fs::path project_root = fs::temp_directory_path() / "rfsim_ext45_shadow_tool";
    const fs::path builtin_manifest = writeManifest(builtin_root, {{"schema_version", 1},
                                                                   {"id", "shared.tool45"},
                                                                   {"name", "Built-in Tool"},
                                                                   {"version", "1.0.0"},
                                                                   {"kind", "external-tool"},
                                                                   {"capabilities", {"generator"}},
                                                                   {"entry", "bin/builtin.py"}});
    const fs::path project_manifest = writeManifest(
        project_root / "rf-sim-extensions" / "shadowing-tool", {{"schema_version", 1},
                                                                {"id", "shared.tool45"},
                                                                {"name", "Built-in Tool"},
                                                                {"version", "9.9.9"},
                                                                {"kind", "external-tool"},
                                                                {"capabilities", {"generator"}},
                                                                {"entry", "bin/evil.py"}});
    ScopedRemove cleanup{builtin_root};

    REQUIRE(fs::exists(builtin_manifest));

    ExtensionManager mgr;
    mgr.rescan(project_root);

    const ExtensionRecord *builtin = nullptr;
    const ExtensionRecord *shadowed = nullptr;
    for (const auto &record : mgr.all()) {
        if (!record.manifest || record.manifest->id != "shared.tool45")
            continue;
        if (record.status == ExtensionStatusKind::Ok)
            builtin = &record;
        else
            shadowed = &record;
    }

    REQUIRE(builtin != nullptr);
    REQUIRE(shadowed != nullptr);
    REQUIRE(builtin->manifest_path == builtin_manifest);
    REQUIRE_FALSE(mgr.isProjectLocal(*builtin));
    REQUIRE(shadowed->status == ExtensionStatusKind::Shadowed);
    REQUIRE(shadowed->manifest_path == project_manifest);
    REQUIRE(mgr.isProjectLocal(*shadowed));
    REQUIRE(shadowed->shadow_detail.find(builtin_manifest.string()) != std::string::npos);

    // Only the built-in tool is launchable: a project cannot take over an id
    // the user already knows from the Tools menu.
    std::vector<const ExtensionManifest *> collisions;
    for (const auto *tool : mgr.externalTools()) {
        if (tool && tool->id == "shared.tool45")
            collisions.push_back(tool);
    }

    REQUIRE(collisions.size() == 1);
    REQUIRE(collisions.front()->entry_path ==
            fs::weakly_canonical(builtin_root / "bin" / "builtin.py"));
}
