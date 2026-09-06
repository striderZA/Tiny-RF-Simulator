// Regression coverage for issue #77: a failed project save (unwritable target
// path) must not clear the app's dirty state or replace the current project
// path, so the user keeps the chance to retry instead of losing track of
// unsaved changes. `ProjectSerializer::save()` reports open/write/flush/close
// failures to the caller and `RfSimulatorApp::saveProject()` acts on the
// result; successful saves still adopt the path, clear dirty state, and
// round-trip.
//
// Standalone executable (not part of the main `tests` binary) because the
// MinGW-w64 toolchain silently drops TEST_CASE registrations beyond a ceiling
// in tests.exe; these cases must actually run on every CI platform.
#include "app.h"
#include "imgui.h"
#include "imnodes.h"
#include "implot.h"
#include <catch2/catch_test_macros.hpp>
#include <filesystem>
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

// RAII guard for temp project files: removes the file on scope exit so an
// assertion failure unwinding the test never leaks issue77_*.rfsim.
struct TempFile {
    fs::path path;
    std::string str() const { return path.string(); }
    ~TempFile() {
        std::error_code ec;
        fs::remove(path, ec);
    }
};

// Unique temp path per call so parallel test processes don't collide.
TempFile uniqueTempFile(const char *stem) {
    static unsigned sequence = 0;
    return TempFile{fs::temp_directory_path() /
                    (std::string(stem) + std::to_string(++sequence) + ".rfsim")};
}

// A path whose parent directory does not exist: opening the ofstream fails on
// every platform, deterministically reproducing a failed save.
fs::path unwritablePath(const char *stem) {
    static unsigned sequence = 0;
    return fs::temp_directory_path() / (std::string(stem) + std::to_string(++sequence)) /
           "project.rfsim";
}

} // namespace

// ---------------------------------------------------------------------------
// Failed save: project stays dirty and the current path is preserved.
// ---------------------------------------------------------------------------
TEST_CASE_METHOD(ImGuiFixture, "Failed save keeps project dirty and preserves the current path",
                 "[issue77][project]") {
    const auto good = uniqueTempFile("issue77_good");
    const auto good2 = uniqueTempFile("issue77_good2");
    const auto bad = unwritablePath("issue77_missing_parent");

    RfSimulatorApp app;

    // Successful save adopts the path and clears dirty state.
    app.saveProject(good.str());
    REQUIRE_FALSE(app.isDirty());
    REQUIRE(app.m_current_project_path == good.str());

    // A failed save (unwritable target) must leave both untouched.
    app.testMakeDirty();
    REQUIRE(app.isDirty());
    app.saveProject(bad.string());
    REQUIRE(app.isDirty());
    REQUIRE(app.m_current_project_path == good.str());
    REQUIRE_FALSE(fs::exists(bad));

    // A later successful save still clears dirty state and adopts the path.
    app.saveProject(good2.str());
    REQUIRE_FALSE(app.isDirty());
    REQUIRE(app.m_current_project_path == good2.str());
}

// ---------------------------------------------------------------------------
// Successful save after a failed save still round-trips: the failure handling
// must not disturb the success path (the retried write loads back cleanly).
// ---------------------------------------------------------------------------
TEST_CASE_METHOD(ImGuiFixture, "Successful save after a failed save still round-trips",
                 "[issue77][project]") {
    const auto good = uniqueTempFile("issue77_good3");
    const auto bad = unwritablePath("issue77_missing_parent_c");
    {
        RfSimulatorApp app;
        app.testMakeDirty();
        app.saveProject(bad.string());
        REQUIRE(app.isDirty());
        REQUIRE(app.m_current_project_path.empty());

        // The retry succeeds and produces a loadable file.
        app.saveProject(good.str());
        REQUIRE_FALSE(app.isDirty());
        REQUIRE(app.m_current_project_path == good.str());
        REQUIRE(fs::exists(good.path));
    }
    {
        // The success path is unchanged: the written file loads back cleanly.
        RfSimulatorApp reload;
        reload.loadProject(good.str());
        REQUIRE_FALSE(reload.isDirty());
        REQUIRE(reload.componentCount() == 2); // default gen + amp
    }
}

// ---------------------------------------------------------------------------
// Failed first save ("Save As" with no prior path): nothing is adopted and
// the project stays dirty, so Ctrl+S keeps offering the dialog instead of
// silently retargeting a path that was never written.
// ---------------------------------------------------------------------------
TEST_CASE_METHOD(ImGuiFixture,
                 "Failed save without a current path leaves the project dirty and untitled",
                 "[issue77][project]") {
    const auto bad = unwritablePath("issue77_missing_parent_b");

    RfSimulatorApp app;
    REQUIRE(app.m_current_project_path.empty());

    app.testMakeDirty();
    app.saveProject(bad.string());

    REQUIRE(app.isDirty());
    REQUIRE(app.m_current_project_path.empty());
    REQUIRE_FALSE(fs::exists(bad));
}
