#include "amplifier_digitizer_widget.h"
#include "app.h"
#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <imgui.h>
#include <imnodes.h>
#include <implot.h>

struct ImGuiFixture {
    ImGuiFixture() {
        ImGui::CreateContext();
        ImPlot::CreateContext();
        ImNodes::CreateContext();
        ImGui::GetIO().DisplaySize = ImVec2(1280.0f, 720.0f);
        unsigned char *pixels = nullptr;
        int width = 0;
        int height = 0;
        ImGui::GetIO().Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
    }
    ~ImGuiFixture() {
        ImNodes::DestroyContext();
        ImPlot::DestroyContext();
        ImGui::DestroyContext();
    }
};

TEST_CASE_METHOD(ImGuiFixture, "AmplifierDigitizerWidget draws closed-safe",
                 "[amp_import_widget]") {
    AmplifierDigitizerWidget widget;
    bool open = true;
    ImGui::NewFrame();
    REQUIRE_NOTHROW(widget.draw(&open));
    ImGui::Render();
}

TEST_CASE_METHOD(ImGuiFixture, "RfSimulatorApp exports imported amplifier and refreshes library",
                 "[amp_import_widget][app]") {
    namespace fs = std::filesystem;
    const fs::path project = fs::temp_directory_path() / "rfsim_amp_import_app";
    std::error_code ec;
    fs::remove_all(project, ec);
    fs::create_directories(project);

    RfSimulatorApp app;
    app.m_current_project_path = (project / "demo.rfsim").string();
    app.openAmplifierImportWizard();
    auto *wizard = app.testAmplifierDigitizerWidget();
    REQUIRE(wizard != nullptr);

    auto &model = wizard->model();
    model.setPartNumber("APP-AMP");
    model.setManufacturer("WizardVendor");
    model.setAxisMode(DigitizerCurveKind::Gain, false);
    model.setAxisMode(DigitizerCurveKind::NoiseFigure, false);
    REQUIRE(model.setCalibration(DigitizerCurveKind::Gain, {{0.0, 1.0e8}, {100.0, 2.0e8}},
                                 {{100.0, 10.0}, {0.0, 20.0}}));
    REQUIRE(model.setCalibration(DigitizerCurveKind::NoiseFigure, {{0.0, 1.0e8}, {100.0, 2.0e8}},
                                 {{100.0, 1.0}, {0.0, 3.0}}));
    REQUIRE(model.addPoint(DigitizerCurveKind::Gain, 0.0, 0.0));
    REQUIRE(model.addPoint(DigitizerCurveKind::Gain, 100.0, 100.0));
    REQUIRE(model.addPoint(DigitizerCurveKind::NoiseFigure, 0.0, 0.0));
    REQUIRE(model.addPoint(DigitizerCurveKind::NoiseFigure, 100.0, 100.0));

    wizard->requestExportForTest();
    app.m_show_node_editor = false;
    app.m_show_spectrum = false;
    app.m_show_properties = false;
    app.m_show_log = false;
    app.m_show_help = false;

    ImGui::NewFrame();
    REQUIRE_NOTHROW(app.draw_ui());
    ImGui::Render();

    const auto defs = app.testLibrary().all();
    REQUIRE(std::any_of(defs.begin(), defs.end(), [](const ComponentDefinition *def) {
        return def->part_number == "APP-AMP";
    }));

    fs::remove_all(project, ec);
}
