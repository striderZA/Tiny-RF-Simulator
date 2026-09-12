// Regression coverage for issue #113: a failed project load must not leave the
// previous file as the Ctrl+S save target, invalid `window_state`/`graph_state`
// fields must be rejected before any reset (so a corrupt optional scalar never
// discards the live project), and a failed save must leave the original file
// byte-identical (temp + rename).
//
// Standalone executable (not part of the main `tests` binary) because the
// MinGW-w64 toolchain silently drops TEST_CASE registrations beyond a ceiling
// in tests.exe; these cases must actually run on every CI platform.
#include "app.h"
#include "imgui.h"
#include "imnodes.h"
#include "implot.h"
#include "signal_generator_engine.h"
#include <catch2/catch_test_macros.hpp>
#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>
#include <random>
#include <string>

namespace fs = std::filesystem;

namespace {

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

// Owns a unique temp directory per test; the random suffix keeps parallel
// CTest *processes* from colliding (a per-process counter alone would not).
struct TempTree {
    fs::path dir;
    explicit TempTree(const char *stem) {
        static std::mt19937_64 rng{std::random_device{}()};
        const auto suffix = std::to_string(rng());
        dir = fs::temp_directory_path() / ("rf_issue113_" + std::string(stem) + "_" + suffix);
        std::error_code ec;
        fs::remove_all(dir, ec);
        fs::create_directories(dir, ec);
    }
    ~TempTree() {
        std::error_code ec;
        fs::remove_all(dir, ec);
    }
    fs::path file(const std::string &name) const { return dir / name; }
};

void writeText(const fs::path &path, const std::string &text) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out << text;
}

void writeJson(const fs::path &path, const nlohmann::json &j) { writeText(path, j.dump(2)); }

std::string readText(const fs::path &path) {
    std::ifstream in(path, std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

nlohmann::json readJson(const fs::path &path) {
    nlohmann::json j;
    std::ifstream in(path);
    in >> j;
    return j;
}

// Mirrors the Ctrl+S dispatch in `RfSimulatorApp::draw_ui()`: with a current
// path it saves there, otherwise it would fall through to Save As. Tests use
// this to prove a stale path can no longer reach the previous file.
void ctrlS(RfSimulatorApp &app) {
    if (!app.m_current_project_path.empty())
        app.saveProject(app.m_current_project_path);
}

} // namespace

// ---------------------------------------------------------------------------
// A wrong-shaped top-level section fails the load and resets, so the previous
// file must be dropped as the save target; a subsequent Ctrl+S cannot reach it.
// ---------------------------------------------------------------------------
TEST_CASE_METHOD(ImGuiFixture,
                 "Issue #113: section-shape load failure clears the previous save target",
                 "[issue113][project]") {
    TempTree tree("path");
    const auto old_path = tree.file("old.rfsim");

    RfSimulatorApp app;
    app.saveProject(old_path.string());
    REQUIRE(app.m_current_project_path == old_path.string());
    const std::string old_bytes = readText(old_path);
    REQUIRE_FALSE(old_bytes.empty());

    const auto check_reset = [&](const std::string &name, const std::string &body) {
        // Reload the good project so each case starts from a valid, clean state.
        app.loadProject(old_path.string());
        REQUIRE(app.m_current_project_path == old_path.string());
        REQUIRE_FALSE(app.isDirty());

        const auto corrupt = tree.file(name);
        writeText(corrupt, body);
        app.loadProject(corrupt.string());

        REQUIRE(app.componentCount() == 0);
        REQUIRE(app.m_current_project_path.empty());
        REQUIRE(app.isDirty());
        ctrlS(app); // no path -> would open Save As; must not touch old.rfsim
        REQUIRE(readText(old_path) == old_bytes);
    };

    SECTION("components is not an array") {
        check_reset("components.rfsim", R"({"components": 5})");
    }
    SECTION("window_state section is not an object") {
        check_reset("window.rfsim", R"({"window_state": 5})");
    }
    SECTION("graph_state section is not an object") {
        check_reset("graph.rfsim", R"({"graph_state": []})");
    }
}

// ---------------------------------------------------------------------------
// A field-level invalid scalar is rejected *before* reset, so the live project
// — and the current save path — are left completely untouched: the boolean set
// by the prior valid load survives, and the counter keeps its value instead of
// wrapping through get<int>() or being reset to a default.
// ---------------------------------------------------------------------------
TEST_CASE_METHOD(ImGuiFixture,
                 "Issue #113: invalid window/graph scalar is rejected without mutating the project",
                 "[issue113][project]") {
    TempTree tree("guard");
    const auto base = tree.file("base.rfsim");
    {
        RfSimulatorApp seed;
        seed.newProject();
        seed.testComponents().add<SignalGeneratorEngine>(10001, seed.testGraphEngine());
        seed.saveProject(base.string());
    }
    // Non-default singleton values so the assertions can tell "untouched" from
    // "reset/defaulted".
    auto base_json = readJson(base);
    base_json["window_state"]["log"] = false;
    base_json["graph_state"]["next_component_id"] = 777;
    writeJson(base, base_json);

    RfSimulatorApp app;
    app.loadProject(base.string());
    REQUIRE(app.componentCount() == 1);
    REQUIRE(app.m_current_project_path == base.string());
    REQUIRE_FALSE(app.isDirty());

    const auto check_rejected = [&](const std::string &name, const std::string &body) {
        const auto malformed = tree.file(name);
        writeText(malformed, body);
        app.loadProject(malformed.string());

        // Rejected: path/project/dirty unchanged (no reset, no partial restore).
        REQUIRE(app.m_current_project_path == base.string());
        REQUIRE_FALSE(app.isDirty());
        REQUIRE(app.componentCount() == 1);

        // Observe the live values through the serializer: both must still be
        // the ones set by the valid load.
        const auto probe = tree.file(name + ".probe.rfsim");
        app.saveProject(probe.string());
        const auto j = readJson(probe);
        REQUIRE(j["window_state"]["log"].get<bool>() == false);
        REQUIRE(j["graph_state"]["next_component_id"].get<int>() == 777);
    };

    SECTION("non-boolean window flag") {
        check_rejected("bad_window.rfsim", R"({"window_state": {"log": 1}})");
    }
    SECTION("oversized next_component_id") {
        check_rejected("bad_graph.rfsim", R"({"graph_state": {"next_component_id": 4294967296}})");
    }
    SECTION("fractional next_component_id") {
        check_rejected("frac_graph.rfsim", R"({"graph_state": {"next_component_id": 1.5}})");
    }
}

// ---------------------------------------------------------------------------
// A save that cannot complete must leave the previous file byte-identical:
// save() serializes to a sibling "<path>.tmp" and only renames on success.
// ---------------------------------------------------------------------------
TEST_CASE_METHOD(ImGuiFixture, "Issue #113: a failed save leaves the original file byte-identical",
                 "[issue113][project]") {
    TempTree tree("atomic");
    const auto target = tree.file("project.rfsim");
    const std::string original = R"({"marker": "original"})";
    writeText(target, original);

    RfSimulatorApp app;

    // Block the documented sibling temp path with a directory so the temp
    // write cannot start; this reproduces a failed save deterministically on
    // every platform (an ofstream cannot open a directory for writing).
    const auto temp = fs::path(target.string() + ".tmp");
    fs::create_directory(temp);

    app.testMakeDirty();
    app.saveProject(target.string());

    REQUIRE(app.isDirty());
    REQUIRE(app.m_current_project_path.empty());
    REQUIRE(readText(target) == original);

    // Unblocking the temp path lets the retry succeed, replacing the target
    // and leaving no temp file behind (issue #77's retry contract).
    std::error_code ec;
    fs::remove(temp, ec);
    app.saveProject(target.string());

    REQUIRE_FALSE(app.isDirty());
    REQUIRE(app.m_current_project_path == target.string());
    REQUIRE_FALSE(fs::exists(temp));
    REQUIRE(readText(target) != original);
}
