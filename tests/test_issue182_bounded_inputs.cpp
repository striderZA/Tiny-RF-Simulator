// Issue #182: utils::inputDouble() and utils::inputFrequency() bound what the
// user can enter, not what the engine holds. A value that arrived outside the
// limits (a library definition, a loaded project, an API call) is shown as it
// is, logged once rather than on every frame, and changed only by a user edit.
// The edit is clamped, except that an edit clamping would reverse (a `+` step
// on a value above the range) is ignored instead of lowering the value.
//
// The fields are drawn in real headless ImGui frames and edited with real
// clicks on their step buttons; the inspector case draws the amplifier
// properties exactly as the Properties panel does.
#include "amplifier_engine.h"
#include "circuit_runtime.h"
#include "editor_commands.h"
#include "graph_editor_actions.h"
#include "imgui.h"
#include "inspector_panel.h"
#include "logging_core.h"
#include "node_graph_engine.h"
#include "utils.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <functional>
#include <string>

namespace {

struct ImGuiFixture {
    ImGuiFixture() {
        ImGui::CreateContext();
        // A bare context has a (-1,-1) DisplaySize sentinel and an imgui.ini
        // path, and with no renderer backend the font atlas must be built
        // explicitly (NewFrame() asserts TexIsBuilt).
        ImGui::GetIO().DisplaySize = ImVec2(1280, 720);
        ImGui::GetIO().IniFilename = nullptr;
        unsigned char *atlas_pixels = nullptr;
        int atlas_w = 0;
        int atlas_h = 0;
        ImGui::GetIO().Fonts->GetTexDataAsRGBA32(&atlas_pixels, &atlas_w, &atlas_h);
    }
    ~ImGuiFixture() { ImGui::DestroyContext(); }
};

// Warnings logged so far whose message mentions `field`.
std::size_t warningsAbout(const std::string &field) {
    std::size_t count = 0;
    for (const LogEntry &entry : LoggerCore::instance().entries())
        if (entry.level == Level::Warn && entry.message.find(field) != std::string::npos)
            ++count;
    return count;
}

// The rect of the last item a frame drew, and the side of a field's square
// step buttons.
struct FieldRect {
    ImVec2 min;
    ImVec2 max;
    float button = 0.0f;
};

// One headless frame drawing `field` in a pinned window, so a rect read on one
// frame is still valid on the next. Returns what `field` returned.
bool drawFrame(const std::function<bool()> &field, FieldRect &rect) {
    ImGui::NewFrame();
    ImGui::SetNextWindowPos(ImVec2(20.0f, 20.0f), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(480.0f, 360.0f), ImGuiCond_Always);
    ImGui::Begin("Bounded input");
    const bool changed = field();
    rect = {ImGui::GetItemRectMin(), ImGui::GetItemRectMax(), ImGui::GetFrameHeight()};
    ImGui::End();
    ImGui::Render();
    return changed;
}

enum class Step { Minus, Plus };

// A real click on a step button of a field drawn with a hidden label: its rect
// then ends at the `+` button, with `-` one button plus ItemInnerSpacing to the
// left. The pointer is parked off the window for a frame, pressed on the next
// and released on the one after. Returns whether any of those frames reported
// a change.
bool clickStep(Step step, const std::function<bool()> &field, FieldRect &rect) {
    const float plus_x = rect.max.x - rect.button * 0.5f;
    const float x =
        step == Step::Plus ? plus_x : plus_x - rect.button - ImGui::GetStyle().ItemInnerSpacing.x;
    const float y = rect.min.y + rect.button * 0.5f;
    bool changed = false;
    ImGui::GetIO().AddMousePosEvent(2.0f, 2.0f);
    changed = drawFrame(field, rect) || changed;
    ImGui::GetIO().AddMousePosEvent(x, y);
    ImGui::GetIO().AddMouseButtonEvent(0, true);
    changed = drawFrame(field, rect) || changed;
    ImGui::GetIO().AddMouseButtonEvent(0, false);
    changed = drawFrame(field, rect) || changed;
    return changed;
}

} // namespace

TEST_CASE("boundedEdit clamps a user edit and never moves against it", "[issue182]") {
    // Inside the range an edit is taken as entered, and clamped at a limit.
    CHECK(utils::boundedEdit(10.0, 12.0, -10.0, 40.0) == 12.0);
    CHECK(utils::boundedEdit(38.0, 43.0, -10.0, 40.0) == 40.0);
    // No edit, or an edit that clamps back onto the current value, writes nothing.
    CHECK_FALSE(utils::boundedEdit(45.0, 45.0, -10.0, 40.0).has_value());
    CHECK_FALSE(utils::boundedEdit(40.0, 41.0, -10.0, 40.0).has_value());
    // Above the range: a step up is ignored, a step down lands on the limit,
    // and a value typed inside the range is taken as entered.
    CHECK_FALSE(utils::boundedEdit(45.0, 46.0, -10.0, 40.0).has_value());
    CHECK(utils::boundedEdit(45.0, 44.0, -10.0, 40.0) == 40.0);
    CHECK(utils::boundedEdit(45.0, 30.0, -10.0, 40.0) == 30.0);
    // Below the range, mirrored.
    CHECK_FALSE(utils::boundedEdit(-20.0, -21.0, -10.0, 40.0).has_value());
    CHECK(utils::boundedEdit(-20.0, -19.0, -10.0, 40.0) == -10.0);
}

TEST_CASE_METHOD(ImGuiFixture, "inputDouble shows and keeps an out-of-range value", "[issue182]") {
    // An amplifier gain authored at 45 dB (the library allows up to 100 dB) in a
    // field with the inspector's -10..40 dB range.
    double gain = 45.0;
    const auto field = [&] {
        return utils::inputDouble("##gain", gain, 1, 10, "%.1f", -10.0, 40.0);
    };
    FieldRect rect;
    const std::size_t warned = warningsAbout("gain:");

    // Drawing never changes the value, and the value is logged once, not per frame.
    for (int frame = 0; frame < 30; ++frame) {
        REQUIRE_FALSE(drawFrame(field, rect));
        REQUIRE(gain == 45.0);
    }
    CHECK(warningsAbout("gain:") == warned + 1);

    // `+` on a value above the range is ignored instead of lowering it to 40.
    CHECK_FALSE(clickStep(Step::Plus, field, rect));
    CHECK(gain == 45.0);

    // `-` is a deliberate edit: clamped into the range, at its upper limit.
    CHECK(clickStep(Step::Minus, field, rect));
    CHECK(gain == 40.0);
    REQUIRE_FALSE(drawFrame(field, rect));
    CHECK(warningsAbout("gain:") == warned + 1);

    // A value that leaves the range again is logged again, once, even the one
    // already reported; so is a different out-of-range value.
    gain = 45.0;
    for (int frame = 0; frame < 5; ++frame)
        REQUIRE_FALSE(drawFrame(field, rect));
    CHECK(warningsAbout("gain:") == warned + 2);
    gain = 50.0;
    for (int frame = 0; frame < 5; ++frame)
        REQUIRE_FALSE(drawFrame(field, rect));
    CHECK(gain == 50.0);
    CHECK(warningsAbout("gain:") == warned + 3);
}

TEST_CASE_METHOD(ImGuiFixture, "inputDouble still clamps an edit made inside its range",
                 "[issue182]") {
    double gain = 38.0;
    const auto field = [&] {
        return utils::inputDouble("##in_range", gain, 5, 10, "%.1f", -10.0, 40.0);
    };
    FieldRect rect;
    const std::size_t warned = warningsAbout("in_range:");
    REQUIRE_FALSE(drawFrame(field, rect));
    REQUIRE_FALSE(drawFrame(field, rect));

    // 38 + 5 overshoots the range and is clamped to its limit...
    CHECK(clickStep(Step::Plus, field, rect));
    CHECK(gain == 40.0);
    // ...and a step that clamps back onto the limit reports no change, so it
    // cannot mark the project dirty.
    CHECK_FALSE(clickStep(Step::Plus, field, rect));
    CHECK(gain == 40.0);
    CHECK(warningsAbout("in_range:") == warned);
}

TEST_CASE_METHOD(ImGuiFixture, "inputFrequency shows and keeps an out-of-range frequency",
                 "[issue182]") {
    // A mixer LO at 150 GHz (the library allows up to 1 THz) in the inspector's
    // 0..100 GHz field, shown and stepped in MHz.
    double lo_Hz = 150e9;
    const auto field = [&] {
        return utils::inputFrequency("##lo", lo_Hz, 1.0, 100.0, "%.0f", 0.0, 100e9);
    };
    FieldRect rect;
    const std::size_t warned = warningsAbout("lo:");
    for (int frame = 0; frame < 30; ++frame) {
        REQUIRE_FALSE(drawFrame(field, rect));
        REQUIRE(lo_Hz == 150e9);
    }
    CHECK(warningsAbout("lo:") == warned + 1);

    CHECK_FALSE(clickStep(Step::Plus, field, rect));
    CHECK(lo_Hz == 150e9);
    CHECK(clickStep(Step::Minus, field, rect));
    CHECK(lo_Hz == 100e9);
}

TEST_CASE_METHOD(ImGuiFixture, "inputFrequency steps a below-range value only up, in its unit",
                 "[issue182]") {
    // An analyzer RBW driven to 100 Hz through the unclamped engine setter, in
    // the widget's 1 kHz..100 MHz field shown and stepped in kHz.
    double rbw_Hz = 100.0;
    const auto field = [&] {
        return utils::inputFrequency("##rbw", rbw_Hz, 1.0, 10.0, "%.0f", 1e3, 100e6, 1e3);
    };
    FieldRect rect;
    for (int frame = 0; frame < 3; ++frame)
        REQUIRE_FALSE(drawFrame(field, rect));
    REQUIRE(rbw_Hz == 100.0);

    // `-` would take it further below the range: ignored, not raised to 1 kHz.
    CHECK_FALSE(clickStep(Step::Minus, field, rect));
    CHECK(rbw_Hz == 100.0);
    // `+` steps 1 kHz from 0.1 kHz and lands inside the range as stepped.
    CHECK(clickStep(Step::Plus, field, rect));
    CHECK(rbw_Hz == Catch::Approx(1100.0));
}

TEST_CASE_METHOD(ImGuiFixture,
                 "the inspector shows an out-of-range amplifier gain without rewriting it",
                 "[issue182]") {
    // The issue's reproduction: a library amplifier authored with Gain 45 dB,
    // drawn by the Properties panel's amplifier drawer (Gain range -10..40 dB).
    CircuitRuntime runtime;
    GraphEditorActions actions{runtime};
    EditorCommands commands{runtime, actions};
    InspectorPanel panel(runtime.graph(), runtime.components(), commands);
    NodeGraphEngine graph;
    AmplifierEngine amp(1, graph);
    amp.setGain_dB(45.0);

    FieldRect rect;
    const std::size_t warned = warningsAbout("Gain (dB):");
    for (int frame = 0; frame < 30; ++frame) {
        drawFrame(
            [&] {
                panel.drawAmplifierProperties(amp, 0);
                return false;
            },
            rect);
        REQUIRE(amp.gain_dB() == 45.0);
    }
    // One warning for the value, where every frame used to log one.
    CHECK(warningsAbout("Gain (dB):") == warned + 1);
}
