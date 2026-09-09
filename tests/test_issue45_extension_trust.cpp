// Issue #45 regression coverage: project-local extensions cannot execute
// until the user explicitly trusts them, and a duplicate extension id is
// reported instead of silently replacing the manifest that claimed it first.
// Standalone executable (not part of test_extensions.cpp) because the
// MinGW-w64 toolchain silently drops TEST_CASE registrations beyond the
// per-binary ceiling; see tests/CMakeLists.txt.
//
// Covered here:
//  - ExtensionTrustStore persistence: round trip, fail-closed loading,
//    approvals scoped to one directory, revocation, hand-edited rows staying
//    findable under their canonical root, incomplete approvals refused, and a
//    failed write never leaving a memory-only approval,
//  - the app gate: an untrusted project-local tool refuses to run before any
//    workspace is created and raises the trust prompt, and an approval that
//    could not be saved leaves the tool gated, not memory-approved,
//  - grant / deny / revoke transitions, including that trusting is not running,
//  - built-in, global, and never-rescanned roots are not gated, and a
//    project-local data pack is never gated,
//  - the Tools menu payload excludes an untrusted project-local tool and keeps
//    everything the gate does not apply to,
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

// A store path whose parent directory does not exist: opening the ofstream
// fails on every platform, so save() cannot land (same technique as
// test_issue77_save_failure.cpp).
fs::path unwritableStorePath(const char *stem) {
    static unsigned sequence = 0;
    return fs::temp_directory_path() / (std::string(stem) + std::to_string(++sequence)) /
           "extension_trust.json";
}

bool toolsMenuLists(const RfSimulatorApp &app, const std::string &id) {
    for (const auto *tool : app.toolsMenuEntries()) {
        if (tool->id == id)
            return true;
    }
    return false;
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

TEST_CASE("trust store files a hand-edited row under its canonical root",
          "[issue45][trust-store]") {
    const fs::path root = fs::temp_directory_path() / "rfsim_ext45_handedited";
    const fs::path store_path = fs::temp_directory_path() / "rfsim_ext45_handedited.json";
    ScopedRemove cleanup_root{root};
    ScopedRemove cleanup_store{store_path};
    fs::create_directories(root);

    // The spelling a user gets by pasting a Windows path into the file the
    // panel names: native separators and a dot segment, already in the file
    // before this process ever reads it.
    const std::string raw_root =
        root.string() + std::string(1, fs::path::preferred_separator) + ".";
    REQUIRE(raw_root != genericRoot(root));
    REQUIRE(genericRoot(fs::path(raw_root)) == genericRoot(root));

    writeStoreFile(store_path, nlohmann::json({{"schema_version", 1},
                                               {"approvals",
                                                {{{"root", raw_root},
                                                  {"id", "project.handedited"},
                                                  {"version", "1.0.0"},
                                                  {"entry_path", "bin/t.py"}}}}})
                                   .dump());

    ExtensionTrustStore store(store_path);
    REQUIRE(store.isApproved(root));
    const auto entry = store.entryFor(root);
    REQUIRE(entry.has_value());
    REQUIRE(entry->root == genericRoot(root));

    // Revocable, not an orphan nothing can remove: the row leaves the file.
    REQUIRE(store.revoke(root));
    REQUIRE_FALSE(store.isApproved(root));
    const nlohmann::json after_revoke =
        nlohmann::json::parse(std::ifstream(store_path), nullptr, false);
    REQUIRE_FALSE(after_revoke.is_discarded());
    REQUIRE(after_revoke["approvals"].empty());
}

TEST_CASE("trust store drops rows it cannot key or read, and keeps valid siblings",
          "[issue45][trust-store]") {
    const fs::path valid_root = fs::temp_directory_path() / "rfsim_ext45_unkeyable_valid";
    const fs::path store_path = fs::temp_directory_path() / "rfsim_ext45_unkeyable.json";
    ScopedRemove cleanup_valid{valid_root};
    ScopedRemove cleanup_store{store_path};
    fs::create_directories(valid_root);

    writeStoreFile(store_path, nlohmann::json({{"schema_version", 1},
                                               {"approvals",
                                                {
                                                    // Empty field: unreadable row.
                                                    {{"root", genericRoot(valid_root)},
                                                     {"id", "project.noentry"},
                                                     {"version", "1.0.0"},
                                                     {"entry_path", ""}},
                                                    // No usable root: unkeyable row.
                                                    {{"root", "\u0000"},
                                                     {"id", "project.unresolvable"},
                                                     {"version", "1.0.0"},
                                                     {"entry_path", "bin/t.py"}},
                                                    {{"root", genericRoot(valid_root)},
                                                     {"id", "project.valid"},
                                                     {"version", "1.0.0"},
                                                     {"entry_path", "bin/t.py"}},
                                                }}})
                                   .dump());

    ExtensionTrustStore store(store_path);
    // The first two rows would both key on valid_root; only the complete row
    // may win it, and the whole file still has to load.
    const auto entry = store.entryFor(valid_root);
    REQUIRE(entry.has_value());
    REQUIRE(entry->id == "project.valid");
}

TEST_CASE("trust store refuses an approval its own loader would discard",
          "[issue45][trust-store]") {
    const fs::path root = fs::temp_directory_path() / "rfsim_ext45_incomplete";
    const fs::path store_path = fs::temp_directory_path() / "rfsim_ext45_incomplete.json";
    ScopedRemove cleanup_root{root};
    ScopedRemove cleanup_store{store_path};
    fs::create_directories(root);

    ExtensionTrustStore store(store_path);

    ExtensionManifest missing_entry = makeToolManifest(root, "project.incomplete45", fs::path{});
    REQUIRE_FALSE(store.approve(missing_entry));

    missing_entry.entry_path = root / "bin" / "t.py";
    missing_entry.version.clear();
    REQUIRE_FALSE(store.approve(missing_entry));

    missing_entry.version = "1.0.0";
    missing_entry.id.clear();
    REQUIRE_FALSE(store.approve(missing_entry));

    // Nothing in memory, nothing on disk, nothing a second reader could trip on.
    REQUIRE_FALSE(store.isApproved(root));
    REQUIRE_FALSE(store.entryFor(root).has_value());
    REQUIRE_FALSE(fs::exists(store_path));

    ExtensionTrustStore reloaded(store_path);
    REQUIRE_FALSE(reloaded.isApproved(root));

    // The same manifest, once complete, is still approvable at all.
    REQUIRE(store.approve(makeToolManifest(root, "project.incomplete45", root / "bin" / "t.py")));
    REQUIRE(store.isApproved(root));
}

TEST_CASE("a failed trust write leaves no approval in memory",
          "[issue45][trust-store][fail-closed]") {
    const fs::path root = fs::temp_directory_path() / "rfsim_ext45_writefail";
    const fs::path store_path = unwritableStorePath("rfsim_ext45_writefail");
    ScopedRemove cleanup_root{root};
    fs::create_directories(root);
    REQUIRE_FALSE(fs::exists(store_path.parent_path()));

    ExtensionTrustStore store(store_path);
    REQUIRE_FALSE(store.approve(makeToolManifest(root, "project.writefail45", root / "t.py")));

    // The contract: a decision that could not be persisted does not exist.
    REQUIRE_FALSE(store.isApproved(root));
    REQUIRE_FALSE(store.entryFor(root).has_value());

    const ExtensionTrustStore reader(store_path);
    REQUIRE_FALSE(reader.isApproved(root));
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

    // The gate's domain is project-local external tools only: a data pack in
    // the project's extension root is never gated (issue #45).
    RfSimulatorApp pack_app;
    pack_app.m_extension_trust.setStorePath(project_root / "extension_trust.json");
    pack_app.m_current_project_path = (project_root / "demo.rfsim").string();
    pack_app.refreshExtensions();
    const fs::path pack_root = project_root / "rf-sim-extensions" / "pack";
    ExtensionManifest pack = makeToolManifest(pack_root, "project.pack45", pack_root / "library");
    pack.kind = ExtensionKind::DataPack;
    REQUIRE(pack_app.m_extension_manager.isProjectLocal(pack));
    REQUIRE_FALSE(pack_app.extensionRequiresTrust(pack));
}

TEST_CASE_METHOD(ImGuiFixture, "a trust decision that could not be saved does not arm the tool",
                 "[issue45][app-gate][fail-closed]") {
    const ToolFixture fixture =
        makeProjectTool("rfsim_ext45_writefail_app", "project.echo45writefail");
    ScopedRemove cleanup{fixture.project_root};
    // Only the tool directory is created; the store's parent stays missing so
    // every write to it fails.
    const fs::path store_path = unwritableStorePath("rfsim_ext45_writefail_app");
    REQUIRE_FALSE(fs::exists(store_path.parent_path()));

    RfSimulatorApp app;
    app.m_extension_trust.setStorePath(store_path);
    app.m_current_project_path = (fixture.project_root / "demo.rfsim").string();
    app.refreshExtensions();

    const ExtensionManifest *tool = toolById(app, fixture.id);
    REQUIRE(tool != nullptr);
    const fs::path tool_root = tool->root_dir;

    app.runExternalTool(*tool);
    REQUIRE(app.m_show_extension_trust_prompt);
    app.grantPendingExtensionTrust();

    // The user is told where the decision was supposed to land...
    const std::string message = app.testExtensionResultMessage();
    REQUIRE(message.find("could not be saved") != std::string::npos);
    REQUIRE(message.find(store_path.string()) != std::string::npos);
    // ...the prompt is not stuck on a decision that failed...
    REQUIRE_FALSE(app.m_show_extension_trust_prompt);
    REQUIRE_FALSE(app.m_pending_trust_manifest.has_value());
    // ...and the tool is still gated: no memory-only approval.
    REQUIRE(app.extensionRequiresTrust(*tool));
    REQUIRE_FALSE(app.m_extension_trust.isApproved(tool_root));

    app.runExternalTool(*tool);
    REQUIRE(app.testExtensionResultMessage().find("not trusted") != std::string::npos);
    REQUIRE_FALSE(fs::exists(fixture.side_effect));
    REQUIRE_FALSE(fs::exists(store_path));
}

TEST_CASE_METHOD(ImGuiFixture, "untrusted tools stay out of the Tools menu payload",
                 "[issue45][app-gate][tools-menu]") {
    const fs::path builtin_dir =
        fs::path(PROJECT_SOURCE_DIR) / "extensions" / "rfsim_ext45_menu_builtin";
    const fs::path builtin_manifest = writeManifest(
        builtin_dir, {{"schema_version", 1},
                      {"id", "builtin.menu45"},
                      {"name", "Built-in Menu Tool"},
                      {"version", "1.0.0"},
                      {"kind", "external-tool"},
                      {"capabilities", {"generator"}},
                      {"entry", "bin/builtin.py"},
                      {"menus", {{{"location", "tools"}, {"label", "Built-in Menu Tool"}}}}});
    ScopedRemove cleanup_builtin{builtin_dir};
    REQUIRE(fs::exists(builtin_manifest));

    const ToolFixture fixture = makeProjectTool("rfsim_ext45_menu", "project.echo45menu");
    ScopedRemove cleanup{fixture.project_root};

    RfSimulatorApp app;
    app.m_extension_trust.setStorePath(fixture.project_root / "extension_trust.json");
    app.m_current_project_path = (fixture.project_root / "demo.rfsim").string();
    app.refreshExtensions();

    // Built-in and global tools are unaffected by an empty trust store.
    REQUIRE(toolsMenuLists(app, "builtin.menu45"));

    const ExtensionManifest *tool = toolById(app, fixture.id);
    REQUIRE(tool != nullptr);
    // The tool is discoverable so the panel can offer Trust, but the menu must
    // not carry its manifest-controlled label while it is untrusted.
    REQUIRE_FALSE(toolsMenuLists(app, fixture.id));

    app.runExternalTool(*tool);
    REQUIRE(app.m_show_extension_trust_prompt);
    app.grantPendingExtensionTrust();
    REQUIRE(app.m_extension_trust.isApproved(tool->root_dir));
    REQUIRE(toolsMenuLists(app, fixture.id));
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
