#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include <cstdio>
#include <fstream>
#include "app.h"
#include "imgui.h"
#include "imnodes.h"
#include "implot.h"
#include <nlohmann/json.hpp>

using Catch::Approx;

static std::string tempPath() { return "test_roundtrip.rfsim"; }
static void cleanup() { std::remove(tempPath().c_str()); }

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

TEST_CASE_METHOD(ImGuiFixture, "Round-trip: empty project", "[project_file]") {
    cleanup();
    {
        RfSimulatorApp app;
        app.saveProject(tempPath());
        REQUIRE(app.isDirty() == false);
    }
    {
        RfSimulatorApp app;
        app.loadProject(tempPath());
        REQUIRE(app.isDirty() == false);
        REQUIRE(app.componentCount() == 2); // default gen + amp
    }
    cleanup();
}

TEST_CASE_METHOD(ImGuiFixture, "Round-trip: single generator and amplifier with link", "[project_file]") {
    cleanup();
    {
        RfSimulatorApp app;
        app.saveProject(tempPath());
    }
    {
        RfSimulatorApp app;
        app.loadProject(tempPath());
        REQUIRE(app.componentCount() == 2);
    }
    cleanup();
}

TEST_CASE_METHOD(ImGuiFixture, "Round-trip: zero components after newProject", "[project_file]") {
    cleanup();
    {
        RfSimulatorApp app;
        app.newProject();
        app.saveProject(tempPath());
        REQUIRE(app.componentCount() == 0);
    }
    {
        RfSimulatorApp app;
        app.loadProject(tempPath());
        REQUIRE(app.componentCount() == 0);
    }
    cleanup();
}

TEST_CASE_METHOD(ImGuiFixture, "Discard clears dirty state", "[project_file]") {
    cleanup();
    {
        RfSimulatorApp app;
        REQUIRE(app.componentCount() == 2);
        REQUIRE(app.isDirty() == false);

        // Mark dirty — simulates unsaved changes
        app.testMakeDirty();
        REQUIRE(app.isDirty() == true);

        // Discard via newProject (same path as Discard button for New/Open)
        app.newProject();
        REQUIRE(app.isDirty() == false);
        REQUIRE(app.componentCount() == 0);
    }
    cleanup();
}

TEST_CASE_METHOD(ImGuiFixture, "Discard then save round-trip", "[project_file]") {
    cleanup();
    {
        RfSimulatorApp app;
        app.testMakeDirty();
        REQUIRE(app.isDirty() == true);

        // Save the dirty project
        app.saveProject(tempPath());
        REQUIRE(app.isDirty() == false);
    }
    {
        // Open it fresh — should have default 2 components (save was from dirty default state)
        RfSimulatorApp app;
        app.loadProject(tempPath());
        REQUIRE(app.isDirty() == false);
        REQUIRE(app.componentCount() == 2);
    }
    cleanup();
}

TEST_CASE_METHOD(ImGuiFixture, "Load invalid JSON does not crash", "[project_file]") {
    cleanup();
    {
        std::ofstream out(tempPath());
        out << "not valid json {{{";
    }
    {
        RfSimulatorApp app;
        app.loadProject(tempPath());
        REQUIRE(app.componentCount() == 2);
    }
    cleanup();
}
