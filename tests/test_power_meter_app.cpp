#include "app.h"
#include "imgui.h"
#include "imnodes.h"
#include "implot.h"
#include <catch2/catch_test_macros.hpp>
#include <cstdio>
#include <filesystem>
#include <fstream>
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

fs::path tempPath(const char *stem) {
    static unsigned sequence = 0;
    return fs::temp_directory_path() / (std::string(stem) + std::to_string(++sequence) + ".rfsim");
}
} // namespace

TEST_CASE_METHOD(ImGuiFixture, "Failed project load preserves power meter source",
                 "[power_meter][project]") {
    RfSimulatorApp app;
    const auto generators = app.testComponents().byType<SignalGeneratorEngine>();
    REQUIRE(generators.size() == 1);

    const int source_pin = generators.front()->outputPinId();
    app.testPowerMeterWidget().setSourcePin(source_pin);
    REQUIRE(app.testPowerMeterWidget().sourcePin() == source_pin);

    const fs::path missing = tempPath("rfsim_power_meter_missing_");
    std::error_code ec;
    fs::remove(missing, ec);
    app.loadProject(missing.string());

    REQUIRE(app.componentCount() == 2);
    REQUIRE(app.testPowerMeterWidget().sourcePin() == source_pin);
}

TEST_CASE_METHOD(ImGuiFixture, "Resetting project load clears power meter source",
                 "[power_meter][project]") {
    RfSimulatorApp app;
    const auto generators = app.testComponents().byType<SignalGeneratorEngine>();
    REQUIRE(generators.size() == 1);
    app.testPowerMeterWidget().setSourcePin(generators.front()->outputPinId());

    const fs::path malformed = tempPath("rfsim_power_meter_malformed_");
    {
        std::ofstream out(malformed);
        REQUIRE(out);
        out << R"({"components": 5})";
    }

    app.loadProject(malformed.string());
    std::remove(malformed.string().c_str());

    REQUIRE(app.componentCount() == 0);
    REQUIRE(app.testPowerMeterWidget().sourcePin() == -1);
}
